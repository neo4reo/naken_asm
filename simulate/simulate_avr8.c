/**
 *  naken_asm assembler.
 *  Author: Michael Kohn
 *   Email: mike@mikekohn.net
 *     Web: http://www.mikekohn.net/
 * License: GPL
 *
 * Copyright 2010-2014 by Michael Kohn
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include "asm_avr8.h"
#include "disasm_avr8.h"
#include "simulate_avr8.h"
#include "table_avr8.h"

#define READ_OPCODE(a)(memory_read_m(simulate->memory, a * 2) | \
                       (memory_read_m(simulate->memory, (a * 2) + 1) << 8))
//#define WRITE_RAM(a,b) memory_write_m(simulate->memory, a, b)

#define SREG_SET(bit) (simulate_avr8->sreg |= (1<<bit))
#define SREG_CLR(bit) (simulate_avr8->sreg &= (0xff ^ (1<<bit)))
#define GET_SREG(bit) ((simulate_avr8->sreg & (1<<bit)) == 0 ? 0 : 1)

#define GET_X() (simulate_avr8->reg[26] | (simulate_avr8->reg[27] << 8))
#define GET_Y() (simulate_avr8->reg[28] | (simulate_avr8->reg[29] << 8))
#define GET_Z() (simulate_avr8->reg[30] | (simulate_avr8->reg[31] << 8))

#define PUSH_STACK(n) \
  simulate_avr8->ram[simulate_avr8->sp--] = (n) && 0xff;

#define PUSH_STACK16(n) \
  simulate_avr8->ram[simulate_avr8->sp--] = (n) >> 8; \
  simulate_avr8->ram[simulate_avr8->sp--] = (n) & 0xff;

#define POP_STACK16() \
  (simulate_avr8->ram[simulate_avr8->sp] | \
  (simulate_avr8->ram[simulate_avr8->sp+1] << 8)); simulate_avr8->sp += 2;

#define READ_FLASH(n) memory_read_m(simulate->memory, n)
#define WRITE_FLASH(n,data) memory_write_m(simulate->memory, n, data)

static int stop_running = 0;

static void handle_signal(int sig)
{
  stop_running = 1;
  signal(SIGINT, SIG_DFL);
}

#if 0
static void sp_inc(int *sp)
{
  (*sp) += 2;
  if (*sp > 0xffff) *sp = 0;
}
#endif

struct _simulate *simulate_init_avr8(struct _memory *memory)
{
struct _simulate *simulate;

  simulate = (struct _simulate *)malloc(sizeof(struct _simulate_avr8) +
                                        sizeof(struct _simulate));

  simulate->simulate_init = simulate_init_avr8;
  simulate->simulate_free = simulate_free_avr8;
  simulate->simulate_push = simulate_push_avr8;
  simulate->simulate_set_reg = simulate_set_reg_avr8;
  simulate->simulate_get_reg = simulate_get_reg_avr8;
  simulate->simulate_reset = simulate_reset_avr8;
  simulate->simulate_dump_registers = simulate_dump_registers_avr8;
  simulate->simulate_run = simulate_run_avr8;

  //memory_init(&simulate->memory, 65536, 0);
  simulate->memory = memory;
  simulate_reset_avr8(simulate);
  simulate->usec = 1000000; // 1Hz
  simulate->step_mode = 0;
  simulate->show = 1;       // Show simulation
  simulate->auto_run = 0;   // Will this program stop on a ret from main

  struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
  simulate_avr8->pc = 0;
  simulate_avr8->sp = 0;
  return simulate;
}

void simulate_push_avr8(struct _simulate *simulate, uint32_t value)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;

  simulate_avr8->sp -= 1;
  PUSH_STACK(value);
}

int simulate_set_reg_avr8(struct _simulate *simulate, char *reg_string, uint32_t value)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
int reg;

  while(*reg_string == ' ') { reg_string++; }
  reg = get_register_avr8(reg_string);
  if (reg == -1)
  {
    // Add flags here
    return -1;
  }

  simulate_avr8->reg[reg] = value;

  return 0;
}

uint32_t simulate_get_reg_avr8(struct _simulate *simulate, char *reg_string)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
int reg;

  reg = get_register_avr8(reg_string);
  if (reg == -1)
  {
    printf("Unknown register '%s'\n", reg_string);
    return -1;
  }

  return simulate_avr8->reg[reg];
}

void simulate_reset_avr8(struct _simulate *simulate)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;

  simulate->cycle_count = 0;
  simulate->nested_call_count = 0;
  memset(simulate_avr8->reg, 0, sizeof(simulate_avr8->reg));
  //memory_clear(&simulate->memory);
  simulate_avr8->pc = 0; 
  simulate_avr8->sp = 0; 
  simulate_avr8->sreg = 0; 
  simulate->break_point = -1;
}

void simulate_free_avr8(struct _simulate *simulate)
{
  //memory_free(simulate->memory);
  free(simulate);
}

// cat table/table_avr8.c | grep OP_NONE | sed 's/^.*AVR8_/AVR8_/' | sed 's/ .*$/:/'
static int simulate_execute_avr8_op_none(struct _simulate *simulate, struct _table_avr8 *table_avr8)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;

  switch(table_avr8->opcode)
  {
    case AVR8_SEC:
      SREG_SET(SREG_C);
      return table_avr8->cycles_min;
    case AVR8_CLC:
      SREG_CLR(SREG_C);
      return table_avr8->cycles_min;
    case AVR8_SEN:
      SREG_SET(SREG_N);
      return table_avr8->cycles_min;
    case AVR8_CLN:
      SREG_CLR(SREG_N);
      return table_avr8->cycles_min;
    case AVR8_SEZ:
      SREG_SET(SREG_Z);
      return table_avr8->cycles_min;
    case AVR8_CLZ:
      SREG_CLR(SREG_Z);
      return table_avr8->cycles_min;
    case AVR8_SEI:
      SREG_SET(SREG_I);
      return table_avr8->cycles_min;
    case AVR8_CLI:
      SREG_CLR(SREG_I);
      return table_avr8->cycles_min;
    case AVR8_SES:
      SREG_SET(SREG_S);
      return table_avr8->cycles_min;
    case AVR8_CLS:
      SREG_CLR(SREG_S);
      return table_avr8->cycles_min;
    case AVR8_SEV:
      SREG_SET(SREG_V);
      return table_avr8->cycles_min;
    case AVR8_CLV:
      SREG_CLR(SREG_V);
      return table_avr8->cycles_min;
    case AVR8_SET:
      SREG_SET(SREG_T);
      return table_avr8->cycles_min;
    case AVR8_CLT:
      SREG_CLR(SREG_T);
      return table_avr8->cycles_min;
    case AVR8_SEH:
      SREG_SET(SREG_H);
      return table_avr8->cycles_min;
    case AVR8_CLH:
      SREG_CLR(SREG_H);
      return table_avr8->cycles_min;
    case AVR8_BREAK:
      // Should we do something here?
      return -1;
    case AVR8_NOP:
      return table_avr8->cycles_min;
    case AVR8_SLEEP:
      // Should we do something here?
      return table_avr8->cycles_min;
    case AVR8_WDR:
      // Should we do something here?
      return -1;
    case AVR8_IJMP:
      simulate_avr8->pc = GET_Z();
      return table_avr8->cycles_min;
    case AVR8_EIJMP:
      return -1;
    case AVR8_ICALL:
      PUSH_STACK16(simulate_avr8->pc)
      simulate_avr8->pc = GET_Z();
      simulate->nested_call_count++;
      return table_avr8->cycles_min;
    case AVR8_EICALL:
      return -1;
    case AVR8_RET:
      simulate_avr8->pc = POP_STACK16();
      simulate->nested_call_count--;
      return table_avr8->cycles_min;
    case AVR8_RETI:
      return -1;
    case AVR8_LPM:
      simulate_avr8->reg[0] = READ_FLASH(GET_Z());
      return table_avr8->cycles_min;
    case AVR8_ELPM:
      return -1;
    case AVR8_SPM:
      WRITE_FLASH(GET_Z(), simulate_avr8->reg[0]);
      return table_avr8->cycles_min;
  }

  return -1;
}

static void simulate_execute_avr8_set_sreg_arith(struct _simulate *simulate, uint8_t rd_prev, uint8_t rd, int k)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
int Rd7 = (rd_prev & 0x80) >> 7;
int R7 = (rd & 0x80) >> 7;
int K7 = (k & 0x80) >> 7;
int C = ((Rd7 ^ 1) & (K7)) | (K7 & R7) | (R7 & (Rd7 ^ 1));
int V = (Rd7 & (K7 ^ 1) & (R7 ^ 1)) | ((Rd7 ^ 1) & K7 & R7);
int N = R7; 
int S = N ^ V;
int Rd3 = (rd_prev & 0x08) >> 3;
int R3 = (rd & 0x08) >> 3;
int K3 = (k & 0x08) >> 3;
int H = ((Rd3 ^ 1) & K3) | (K3 & R3) | (R3 & (Rd3 ^ 1));

  if (N == 1) { SREG_SET(SREG_N); } else { SREG_CLR(SREG_N); }
  if (C == 1) { SREG_SET(SREG_C); } else { SREG_CLR(SREG_C); }
  if (rd == 0) { SREG_SET(SREG_Z); } else { SREG_CLR(SREG_Z); }
  if (S == 1) { SREG_SET(SREG_S); } else { SREG_CLR(SREG_S); }
  if (V == 1) { SREG_SET(SREG_V); } else { SREG_CLR(SREG_V); }
  if (H == 1) { SREG_SET(SREG_H); } else { SREG_CLR(SREG_H); }
}

static void simulate_execute_avr8_set_sreg_logic(struct _simulate *simulate, uint8_t rd_prev, uint8_t rd, int k)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
int R7 = (rd & 0x80) >> 7;
int N = R7; 
int S = N ^ 0;

  SREG_CLR(SREG_V);
  if (N == 1) { SREG_SET(SREG_N); } else { SREG_CLR(SREG_N); }
  if (rd == 0) { SREG_SET(SREG_Z); } else { SREG_CLR(SREG_Z); }
  if (S == 1) { SREG_SET(SREG_S); } else { SREG_CLR(SREG_S); }
}

static void simulate_execute_avr8_set_sreg_reg16(struct _simulate *simulate, int rd_prev, int rd)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
int R15 = (rd & 0x8000) >> 15;
int Rdh7 = (rd_prev & 0x0080) >> 7;
int N = R15; 
int V = (Rdh7 ^ 1) & R15;
int S = N ^ V;
int C = (R15 ^ 1) & Rdh7;

  if (S == 1) { SREG_SET(SREG_S); } else { SREG_CLR(SREG_S); }
  if (V == 1) { SREG_SET(SREG_V); } else { SREG_CLR(SREG_V); }
  if (N == 1) { SREG_SET(SREG_N); } else { SREG_CLR(SREG_N); }
  if (rd == 0) { SREG_SET(SREG_Z); } else { SREG_CLR(SREG_Z); }
  if (C == 1) { SREG_SET(SREG_C); } else { SREG_CLR(SREG_C); }
}

static int simulate_execute_avr8_op_reg_imm(struct _simulate *simulate, struct _table_avr8 *table_avr8, uint16_t opcode)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
int rd = ((opcode >> 4) & 0xf) + 16;
int k = ((opcode & 0xf00) >> 4) | (opcode & 0xf);
uint8_t prev = simulate_avr8->reg[rd];
int prev_reg16,reg16;

  switch(table_avr8->opcode)
  {
    case AVR8_ANDI:
      simulate_avr8->reg[rd] &= k;
      simulate_execute_avr8_set_sreg_logic(simulate, prev, simulate_avr8->reg[rd], k); 
      break;
    case AVR8_CPI:
      simulate_avr8->reg[rd] -= k;
      simulate_execute_avr8_set_sreg_arith(simulate, prev, simulate_avr8->reg[rd], k); 
      break;
    case AVR8_LDI:
      simulate_avr8->reg[rd] = k;
      break;
    case AVR8_ORI:
      simulate_avr8->reg[rd] |= k;
      simulate_execute_avr8_set_sreg_logic(simulate, prev, simulate_avr8->reg[rd], k); 
      break;
    case AVR8_SBCI:
      simulate_avr8->reg[rd] = simulate_avr8->reg[rd] - k - GET_SREG(SREG_C);
      simulate_execute_avr8_set_sreg_arith(simulate, prev, simulate_avr8->reg[rd], k); 
    case AVR8_SBR:
      simulate_avr8->reg[rd] &= k;
      simulate_execute_avr8_set_sreg_logic(simulate, prev, simulate_avr8->reg[rd], k); 
    case AVR8_SUBI:
      simulate_avr8->reg[rd] = simulate_avr8->reg[rd] - k;
      simulate_execute_avr8_set_sreg_arith(simulate, prev, simulate_avr8->reg[rd], k); 
    case AVR8_CBR:
      simulate_avr8->reg[rd] &= k ^ 0xff;
      simulate_execute_avr8_set_sreg_logic(simulate, prev, simulate_avr8->reg[rd], k); 
    case AVR8_ADIW:
      prev_reg16 = simulate_avr8->reg[rd] | (simulate_avr8->reg[rd + 1] << 8);
      reg16 = prev_reg16 + k;
      simulate_execute_avr8_set_sreg_reg16(simulate, prev_reg16, reg16); 
    case AVR8_SBIW:
      prev_reg16 = simulate_avr8->reg[rd] | (simulate_avr8->reg[rd + 1] << 8);
      reg16 = prev_reg16 - k;
      simulate_execute_avr8_set_sreg_reg16(simulate, prev_reg16, reg16); 
      break;
  }

  return table_avr8->cycles_min;
}

static int simulate_execute_avr8_op_relative(struct _simulate *simulate, struct _table_avr8 *table_avr8, uint16_t opcode)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
int k = opcode & 0xfff;

  if (k & 800) { k = -(((~k) & 0xfff) + 1); }

  switch(table_avr8->opcode)
  {
    case AVR8_RJMP:
      break;
    case AVR8_RCALL:
      PUSH_STACK16(simulate_avr8->pc)
      break;
  }

  simulate_avr8->pc += k;

  return table_avr8->cycles_min;
}

static int simulate_execute_avr8(struct _simulate *simulate)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
uint16_t opcode;
int cycles = -1;
int pc;

  pc = simulate_avr8->pc * 2;
  opcode = READ_OPCODE(pc);
  //c = get_cycle_count(opcode);
  //if (c > 0) simulate->cycle_count += c;
  simulate_avr8->pc += 1;

  int n = 0;
  while(table_avr8[n].instr != NULL)
  {
    if ((opcode & table_avr8[n].mask) == table_avr8[n].opcode)
    {
      //*cycles_min = table_avr8[n].cycles_min;
      //*cycles_max = table_avr8[n].cycles_max;

      switch(table_avr8[n].type)
      {
        case OP_NONE:
          cycles = simulate_execute_avr8_op_none(simulate, table_avr8);
          break;
        case OP_REG_IMM:
          cycles = simulate_execute_avr8_op_reg_imm(simulate, table_avr8, opcode);
        case OP_RELATIVE:
          cycles = simulate_execute_avr8_op_relative(simulate, table_avr8, opcode);
          break;
        default:
          break;
      }
    }

    n++;
  }

  return cycles;
}

void simulate_dump_registers_avr8(struct _simulate *simulate)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
int n;

  printf("\nSimulation Register Dump\n");
  printf("-------------------------------------------------------------------\n");
  printf(" PC: 0x%04x,  SP: 0x%04x, SREG: I T H S V N Z C = 0x%02x\n"
         "                                %d %d %d %d %d %d %d %d\n",
         simulate_avr8->pc,
         simulate_avr8->sp,
         GET_SREG(SREG_I),
         GET_SREG(SREG_T),
         GET_SREG(SREG_H),
         GET_SREG(SREG_S),
         GET_SREG(SREG_V),
         GET_SREG(SREG_N),
         GET_SREG(SREG_Z),
         GET_SREG(SREG_C),
         simulate_avr8->sreg);

  for (n = 0; n < 32; n++)
  {
    if ((n % 8) == 0)
    {
      //printf("      0x%04x: 0x%02x%02x", SHOW_STACK);
      printf("\n");
      //sp_inc(&sp);
    }
      else
    { printf(" "); }

    char reg[4];
    sprintf(reg, "r%d",n);
    printf("%3s: 0x%02x", reg, simulate_avr8->reg[n]);
  }

  printf(" X=0x%04x, Y=0x%04x, Z=0x%04x\n\n", GET_X(), GET_Y(), GET_Z());
  printf("%d clock cycles have passed since last reset.\n\n", simulate->cycle_count);
}

int simulate_run_avr8(struct _simulate *simulate, int max_cycles, int step)
{
struct _simulate_avr8 *simulate_avr8 = (struct _simulate_avr8 *)simulate->context;
char instruction[128];
int cycles = 0;
int ret;
int pc;
//int c;
int n;

  stop_running = 0;
  signal(SIGINT, handle_signal);

  printf("Running... Press Ctl-C to break.\n");

  while(stop_running == 0)
  {
    pc = simulate_avr8->pc;
    ret = simulate_execute_avr8(simulate);

    if (simulate->show == 1) printf("\x1b[1J\x1b[1;1H");

    //if (c > 0) cycles += c;

    if (simulate->show == 1)
    {
      int disasm_pc = pc;
      simulate_dump_registers_avr8(simulate);

      n = 0;
      while(n < 6)
      {
        int cycles_min,cycles_max;
        int num;
        num = READ_OPCODE(disasm_pc);
        int count = disasm_avr8(simulate->memory, disasm_pc * 2, instruction, &cycles_min, &cycles_max) / 2;
        if (cycles_min == -1) break;

        if (disasm_pc == simulate->break_point) { printf("*"); }
        else { printf(" "); }

        if (n == 0)
        { printf("! "); }
          else
        if (disasm_pc == simulate_avr8->reg[0]) { printf("> "); }
          else
        { printf("  "); }

        if (cycles_min < 1)
        {
          printf("0x%04x: 0x%04x %-40s ?\n", disasm_pc, num, instruction);
        }
          else
        if (cycles_min == cycles_max)
        {
          printf("0x%04x: 0x%04x %-40s %d\n", disasm_pc, num, instruction, cycles_min);
        }
          else
        {
          printf("0x%04x: 0x%04x %-40s %d-%d\n", disasm_pc, num, instruction, cycles_min, cycles_max);
        }

        n = n + count;
        count--;
        disasm_pc++;
        while (count > 0)
        {
          if (disasm_pc == simulate->break_point) { printf("*"); }
          else { printf(" "); }
          num = READ_OPCODE(disasm_pc);
          printf("  0x%04x: 0x%04x\n", disasm_pc, num);
          disasm_pc++;
          count--;
        }
      }
    }

    if (simulate->auto_run == 1 && simulate->nested_call_count < 0) { return 0; }

    if (ret == -1)
    {
      printf("Illegal instruction at address 0x%04x\n", pc);
      return -1;
    }

    if (max_cycles != -1 && cycles > max_cycles) break;
    if (simulate->break_point == simulate_avr8->reg[0])
    {
       printf("Breakpoint hit at 0x%04x\n", simulate->break_point);
      break;
    }

    if (simulate->usec == 0 || step == 1)
    {
      //simulate->step_mode=0;
      signal(SIGINT, SIG_DFL);
      return 0;
    }

#if 0
    if (simulate_avr8->pc == 0xffff)
    {
      printf("Function ended.  Total cycles: %d\n", simulate->cycle_count);
      simulate->step_mode = 0;
      signal(SIGINT, SIG_DFL);
      return 0;
    }
#endif

    usleep(simulate->usec);
  }

  signal(SIGINT, SIG_DFL);
  printf("Stopped.  PC=0x%04x.\n", simulate_avr8->reg[0]);
  printf("%d clock cycles have passed since last reset.\n", simulate->cycle_count);

  return 0;
}

