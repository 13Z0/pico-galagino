/*
 * From upstream Galagino (GPL-3.0, derived from asig/kosmos-cp1). Changed for
 * pico-galagino: true/false are left to stdbool.h when it is included, the
 * sound ROM is reached through a pointer, and the state counts machine cycles
 * (`cycles`, advanced by every tick) so the CPU can be run by its clock.
 */
#ifndef _I8048_H_
#define _I8048_H_

#ifndef false
#define false 0
#endif
#ifndef true
#define true (!false)
#endif

typedef char boolean;

// Bits in PSW
#define CY_BIT  7
#define AC_BIT  6
#define F0_BIT  5
#define BS_BIT  4

#define REGISTER_BANK_0_BASE 0
#define REGISTER_BANK_1_BASE 24

struct i8048_state_S {
  // Interrupt pins and flipflops
  boolean TF; // Timer Flag
  boolean notINT;
  boolean timerInterruptRequested;
  boolean T0;
  boolean T1;
  
  unsigned char T;
  unsigned char A;
  unsigned short PC;
  unsigned char PSW;
  boolean DBF;
  boolean F1;

  boolean externalInterruptsEnabled;
  boolean tcntInterruptsEnabled;
  boolean counterRunning; // Whether counter is bound to T1 (STRT CNT)
  boolean timerRunning; // Whether counter is bound to clock (STRT T)
  long cyclesUntilCount; // prescaler: Number of cycles until we need to increment the count (if counter is bound to clock)
  boolean inInterrupt; // True if handling an interrupt. Reset by RETR

  unsigned char ram[128];

  int p2_state;

  unsigned long cycles; // machine cycles executed (pico-galagino)
};

void i8048_reset(struct i8048_state_S *state);
void i8048_step(struct i8048_state_S *state);

// inlining the rom access gives a significant performance boost
extern const unsigned char *dkong_rom_cpu2;
static inline const unsigned char i8048_rom_read(struct i8048_state_S *state, unsigned short addr) {
  return dkong_rom_cpu2[addr];
}

// ----- functions to be provided externally -----
void i8048_port_write(struct i8048_state_S *, unsigned char, unsigned char);
unsigned char i8048_port_read(struct i8048_state_S *, unsigned char);

unsigned char i8048_xdm_read(struct i8048_state_S *, unsigned char);
void i8048_xdm_write(struct i8048_state_S *, unsigned char, unsigned char);

#endif // _I8048_H_
