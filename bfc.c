/* 
 * BF compiler
 * Copyright 2008 S. Pal
 * Copyright 2011 A. Horn
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h> 
#include <unistd.h>
#include <string.h>

#define STACK_SIZE          1024  /* Default loop stack size */
#define STACK_GROWTH_FACTOR 1.1   /* Increase stack by 10% if it is full */

static const char bfc_usage[] = "bfc [options] ... <file>\n"
                                "Options:\n"
                                " -S       " "   " "Compile only; do not assemble or link\n"
                                " -c       " "   " "Compile and assemble, but do not link\n"
                                " -o <file>" "   " "Write output to file\n"
                                " -s <size>" "   " "Allocate specified number of bytes\n"
                                " -h       " "   " "Display this help and exit\n";

enum stage
{
  COMPILE,   /* Compile only */
  ASSEMBLE,  /* Compile and assemble only */
  LINK       /* Compile, assemble and link */
};

typedef struct info_t info_t;
struct info_t
{
  char *in_filename;       /* BF source code file name */
  char *out_filename;      /* Object code file name */
  enum stage target;       /* Final stage that generates the object code */
  unsigned int cells_size; /* Number of bytes allocated as memory */
};

int setup_info(info_t *info, int argc, char **argv);
void compile(const info_t *info, const char *asm_filename, const char *src_filename);
char *replace_extension(const char *name, char ext);
void usage(const char *msg);
void error(const char *err, ...);

/*
 * Parses the command line, sets the compile options, invokes the
 * functions and commands necessary to generate the output file.
 */
int main(int argc, char **argv)
{
  int long cells_size = 4096;    /* Default number of allocated bytes */
  char *asm_filename;            /* Name of assembly code file */
  char *obj_filename;            /* Name of object code file */
  char *bin_filename = "a.out";  /* Default name of binary file (e.g. ELF file) */
  char *command;                 /* Pointer for external commands */
  size_t len;                    /* Stores string lengths */
  int ok;                        /* Boolean status flag */
  info_t info;                   /* Compilation information */

  info.in_filename = NULL;
  info.out_filename = NULL;
  info.target = LINK; 
  info.cells_size = cells_size;

  ok = setup_info(&info, argc, argv);

  if (!ok) {
    error("Invalid command line arguments; see 'bfc -h'");
  }

  /* If input source code file name is not specified, exit */
  if (info.in_filename == NULL) {
    error("Missing input file; see 'bfc -h'");
  }

  /*
   * Phase 1: Compile
   */

  /* Set name for assembly code filename */
  if (info.target == COMPILE && info.out_filename != NULL) {
    asm_filename = strdup(info.out_filename);
  } else {
    asm_filename = replace_extension(info.in_filename, 's'); 
  }

  /* Compile the source file into IA-32 assembly code */
  compile(&info, asm_filename, info.in_filename);

  /* If compile only option was specified, exit */
  if (info.target == COMPILE) {
    free(asm_filename);
    exit(EXIT_SUCCESS);
  }

  /*
   * Phase 2: Assemble
   */

  /* Set name for object code filename */
  if (info.target == ASSEMBLE && info.out_filename != NULL) {
    obj_filename = strdup(info.out_filename);
  } else {
    obj_filename = replace_extension(info.in_filename, 'o'); 
  }

  /* Prepare command line for GNU as */
  len = strlen("as -o") + strlen(asm_filename) + strlen(obj_filename) + 3;
  if ((command = malloc(len)) == NULL) {
    error("Out of memory while assembling");
  }
  sprintf(command, "as -o %s %s", obj_filename, asm_filename);

  /* Assemble the assembly code into object code */
  system(command);
  free(command);

  /* Assembly code file is not required after assembling */
  unlink(asm_filename);
  free(asm_filename);

  /* If compile and assemble only option was specified, exit */
  if (info.target == ASSEMBLE) {
    free(obj_filename);
    exit(EXIT_SUCCESS);
  }

  /*
   * Phase 3: Link
   */

  /* Override default for executable code filename if specified */
  if (info.target == LINK && info.out_filename != NULL) {
    bin_filename = info.out_filename;
  }

  /* Prepare command line for GNU ld */
  len = strlen("ld -o") + strlen(obj_filename) + strlen(bin_filename) + 3;
  if ((command = malloc(len)) == NULL) {
    error("Out of memory while compiling");
  }
  sprintf(command, "ld -o %s %s", bin_filename, obj_filename);

  /* Link the object code to executable code */
  system(command);
  free(command);

  /* Object code file is not required after linking */
  unlink(obj_filename);
  free(obj_filename);

  exit(EXIT_SUCCESS);
}

/*
 * Compiles the BF source code in src_filename and writes the
 * IA-32 assemble code to the asm_filename.
 */
void compile(const info_t *info, const char *asm_filename, const char *src_filename)
{
  FILE *src;                      /* Source code file */
  FILE *as;                       /* Assembly code file */
  size_t *stack;                  /* Loop stack */
  size_t top = 0;                 /* Next free location in stack */
  size_t stack_size = STACK_SIZE; /* Stack size */
  size_t loop = 0;                /* Used to generate loop labels */
  int c;                          /* Character in BF source code */

  /* Open BF code file */
  src = fopen(src_filename, "r");
  if (src == NULL) {
    error("Could not read file %s", src_filename);
  }

  /* Create assembly code file */
  as = fopen(asm_filename, "w");
  if (as == NULL) {
    error("Could not write file %s", asm_filename);
  }

  /* Create loop stack */
  stack = malloc(stack_size * sizeof(*stack));
  if (stack == NULL) {
    error("Out of memory while creating loop stack of size %zu", stack_size);
  }

  /* Write IA-32 assembly code */
  fprintf(as, ".intel_syntax noprefix\n");

  /* Allocate info->cells_size zeroed bytes */
  fprintf(as, ".section .bss\n");
  fprintf(as, "\t.lcomm cells, %u\n", info->cells_size);

  /* Start instructions */
  fprintf(as, ".section .text\n");
  fprintf(as, ".globl _start\n");
  fprintf(as, "_start:\n");

  /* Assign BSS address to EDI register */
  fprintf(as, "\tmov edi, OFFSET cells\n");
  while ((c = fgetc(src)) != EOF) {
    switch (c) {
    case '>':
      /* Advance pointer by four bytes (i.e. 32 bits) */
      fprintf(as, "\tadd edi, 4\n"); 
      break;
    case '<':
      /* Rewind pointer by four bytes (i.e. 32 bits) */
      fprintf(as, "\tsub edi, 4\n");
      break;
    case '+':
      /* Increment 32-bit byte pointed to by EDI register */
      fprintf(as, "\tinc DWORD PTR [edi]\n");
      break;
    case '-':
      /* Decrement 32-bit byte pointed to by EDI register */
      fprintf(as, "\tdec DWORD PTR [edi]\n");
      break;
    case ',':
      /*
       * Tell kernel via interrupt (0x80) to write (EAX=3)
       * one byte (EDX=1) to standard output (EBX=0).
       * See also OS vector table.
       */
      fprintf(as, "\tmov eax, 3\n");
      fprintf(as, "\tmov ebx, 0\n");
      fprintf(as, "\tmov ecx, edi\n");
      fprintf(as, "\tmov edx, 1\n");
      fprintf(as, "\tint 0x80\n");
      break;
    case '.':
      /*
       * Tell kernel via interrupt (0x80) to read (EAX=4)
       * one byte (EDX=1) from standard input (EBX=1).
       * See also OS vector table.
       */
      fprintf(as, "\tmov eax, 4\n");
      fprintf(as, "\tmov ebx, 1\n");
      fprintf(as, "\tmov ecx, edi\n");
      fprintf(as, "\tmov edx, 1\n");
      fprintf(as, "\tint 0x80\n");
      break;
    case '[':
      if (top == stack_size) {
        /* Resize stack */
        stack_size *= STACK_GROWTH_FACTOR;
        stack = realloc(stack, sizeof(*stack) * stack_size);
        if (stack == NULL) {
          error("Out of memory while increasing loop stack to size: %zu\n", stack_size);
        }
      }
      /* Push new loop label on stack */
      stack[top++] = ++loop;

      fprintf(as, "\tcmp DWORD PTR [edi], 0\n");
      fprintf(as, "\tjz .LE%u\n", loop);
      fprintf(as, ".LB%u:\n", loop);
      break;
    case ']':
      /* Find matching label by popping the stack */
      fprintf(as, "\tcmp DWORD PTR [edi], 0\n");
      fprintf(as, "\tjnz .LB%u\n", stack[--top]);
      fprintf(as, ".LE%u:\n", stack[top]);
      break;
    }
  }

  /* Specify sys_exit function code (from OS vector table) */
  fprintf(as, "mov eax, 1\n");

  /* Specify successful return code for OS */
  fprintf(as, "mov ebx, 0\n");

  /* Tell kernel to perform system call */
  fprintf(as, "int 0x80\n");

  /* Release allocated streams */
  fclose(as);
  fclose(src);
}

/* Parse command line arguments to set info fields */
int setup_info(info_t *info, int argc, char **argv)
{
  char *tail;
  int c;
  int long cells_size;

  /* print bfc_usage instead of getopt diagnostic message */
  opterr = 0;

  while ((c = getopt (argc, argv, "Scho:s:")) != -1) {
    switch (c) {
    case 'S':
      if(info->target > COMPILE) {
        info->target = COMPILE;
      }
      break;
    case 'c':
      if(info->target > ASSEMBLE) {
        info->target = ASSEMBLE;
      }
      break;
    case 'o':
      info->out_filename = optarg;
      break;
    case 's':
      errno = 0;
      cells_size = strtol(optarg, &tail, 0);
      if(errno || *tail != '\0' || cells_size <= 0) {
        return 0;
      }

      info->cells_size = cells_size;
      break;
    default:
      return 0;
    }
  }

  if (optind < argc) {
    info->in_filename = argv[optind];
    return 1;
  }

  return 0;
}

/*
 * Constructs a new string by replacing the file extension with the
 * specified extension. If the filename has no extension, the extension
 * character is simply appended to the filename. The function returns
 * the address of the new string that has been constructed.
 */
char *replace_extension(const char *name, char ext)
{
  char *new_name;
  const char *dot = strrchr(name, '.');
  const size_t len = (dot == NULL ? strlen(name) : dot - name);

  new_name = malloc(len + 3);
  if (new_name == NULL) {
    error("Out of memory while changing extension of %s to %c", name, ext);
  }

  strncpy(new_name, name, len);
  new_name[len] = '.';
  new_name[len+1] = ext;
  new_name[len+2] = '\0';

  return new_name;
}

void error(const char *err, ...)
{
  va_list params;

  va_start(params, err);
  fprintf(stderr, "bfc: ");
  vfprintf(stderr, err, params);
  fprintf(stderr, "\n");
  va_end(params);

  exit(EXIT_FAILURE);
}

void usage(const char *msg)
{
  fprintf(stderr, "Usage: %s", msg);
  exit(EXIT_FAILURE);
}
