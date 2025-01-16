#ifndef LR35902_H
#define LR35902_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LR35902_FAST_TABLE
    #define LR35902_FAST_TABLE
#endif

#ifndef LR35902_FAST_CODE
    #define LR35902_FAST_CODE
#endif

#ifndef LR35902_FORCE_INLINE
    #define LR35902_FORCE_INLINE
#endif

#ifdef LR35902_STATIC
    #define LR35902_DEF static
    #define LR35902_API_INLINE inline
    #define LR35902_API_FORCE_INLINE LR35902_FORCE_INLINE
#else
    #define LR35902_DEF extern
    #define LR35902_API_INLINE
    #define LR35902_API_FORCE_INLINE
#endif

struct LR35902 {
    void* userdata;
#ifndef LR35902_NO_CYCLES
    unsigned short cycles;
#endif
    unsigned short SP;
    unsigned short PC;
    unsigned char registers[0x8];
    unsigned char IME_delay;
    unsigned char IME;
    unsigned char HALT;
#ifdef LR35902_BUILTIN_INTERRUTS
    unsigned char IF;
    unsigned char IE;
#endif
};

LR35902_DEF LR35902_API_FORCE_INLINE void LR35902_FAST_CODE LR35902_run(struct LR35902*);

/* normal reset, use this if you're using the bios. */
// LR35902_DEF void LR35902_reset(struct LR35902*);
/* hle the dmg bios. */
LR35902_DEF void LR35902_reset_dmg(struct LR35902*);
/* hle the cgb bios. */
LR35902_DEF void LR35902_reset_cgb(struct LR35902*);
/* hle the gba bios. */
LR35902_DEF void LR35902_reset_agb(struct LR35902*);
/* call this to setup gbs player.  */
LR35902_DEF void LR35902_reset_gbs(struct LR35902*, unsigned short pc, unsigned short sp, unsigned char a);

/* need to be defined */
LR35902_DEF LR35902_API_FORCE_INLINE unsigned char LR35902_read(void* user, unsigned short addr);
LR35902_DEF LR35902_API_FORCE_INLINE void LR35902_write(void* user, unsigned short addr, unsigned char value);

#ifndef LR35902_BUILTIN_INTERRUTS
LR35902_DEF LR35902_API_FORCE_INLINE unsigned char LR35902_FAST_CODE LR35902_get_interrupts(void* user);
LR35902_DEF LR35902_API_FORCE_INLINE void LR35902_FAST_CODE LR35902_handle_interrupt(void* user, unsigned char interrupt);
#endif

/* called on halt */
#ifdef LR35902_ON_HALT
LR35902_DEF void LR35902_on_halt(void* user);
#endif

/* called on stop */
#ifdef LR35902_ON_STOP
LR35902_DEF void LR35902_on_stop(void* user);
#endif

#ifdef LR35902_READ16
LR35902_DEF LR35902_API_FORCE_INLINE unsigned short LR35902_read16(void* user, unsigned short addr);
#endif

#ifdef LR35902_WRITE16
LR35902_DEF LR35902_API_FORCE_INLINE void LR35902_write16(void* user, unsigned short addr, unsigned short value);
#endif

#ifdef LR35902_STACK_PUSH
LR35902_DEF LR35902_API_FORCE_INLINE void LR35902_stack_push(void* user, unsigned short addr, unsigned short value);
#endif

#ifdef LR35902_STACK_POP
LR35902_DEF LR35902_API_FORCE_INLINE unsigned short LR35902_stack_pop(void* user, unsigned short addr);
#endif

#ifdef LR35902_IMPLEMENTATION
#include <assert.h>

#ifndef LR35902_NO_CYCLES
static const unsigned char LR35902_FAST_TABLE CYCLE_TABLE[0x100] = {
	4,12,8,8,4,4,8,4,20,8,8,8,4,4,8,4,4,
	12,8,8,4,4,8,4,12,8,8,8,4,4,8,4,8,
	12,8,8,4,4,8,4,8,8,8,8,4,4,8,4,8,
	12,8,8,12,12,12,4,8,8,8,8,4,4,8,4,4,
	4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,
	4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,
	4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,8,
	8,8,8,8,8,4,8,4,4,4,4,4,4,8,4,4,
	4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,
	4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,
	4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,4,
	4,4,4,4,4,8,4,4,4,4,4,4,4,8,4,8,
	12,12,16,12,16,8,16,8,16,12,4,12,24,8,16,8,
	12,12,0,12,16,8,16,8,16,12,0,12,0,8,16,12,
	12,8,0,0,16,8,16,16,4,16,0,0,0,8,16,12,
	12,8,4,0,16,8,16,12,8,16,4,0,0,8,16,
};

static const unsigned char LR35902_FAST_TABLE CYCLE_TABLE_CB[0x100] = {
	8,8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,12,8,8,8,8,8,8,8,12,8,8,
	8,8,8,8,8,12,8,8,8,8,8,8,8,12,8,8,
	8,8,8,8,8,12,8,8,8,8,8,8,8,12,8,8,
	8,8,8,8,8,12,8,8,8,8,8,8,8,12,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,8,
	8,8,8,8,8,16,8,8,8,8,8,8,8,16,8,
};
    #define set_cycles(count) cpu->cycles = count
    #define add_cycles(count) cpu->cycles += count

#else
    #define set_cycles(count)
    #define add_cycles(count)
#endif

#define REG_B cpu->registers[0]
#define REG_C cpu->registers[1]
#define REG_D cpu->registers[2]
#define REG_E cpu->registers[3]
#define REG_H cpu->registers[4]
#define REG_L cpu->registers[5]
#define REG_F cpu->registers[6]
#define REG_A cpu->registers[7]
#define REG(v) cpu->registers[(v) & 0x7]
#define REG_SP cpu->SP
#define REG_PC cpu->PC

#define REG_BC ((REG_B << 8) | REG_C)
#define REG_DE ((REG_D << 8) | REG_E)
#define REG_HL ((REG_H << 8) | REG_L)
#define REG_AF ((REG_A << 8) | (REG_F & 0xF0))
#define SET_REG_BC(v) REG_B = (((v) >> 8) & 0xFF); REG_C = ((v) & 0xFF)
#define SET_REG_DE(v) REG_D = (((v) >> 8) & 0xFF); REG_E = ((v) & 0xFF)
#define SET_REG_HL(v) REG_H = (((v) >> 8) & 0xFF); REG_L = ((v) & 0xFF)
#define SET_REG_AF(v) REG_A = (((v) >> 8) & 0xFF); REG_F = ((v) & 0xF0)

#define FLAG_C (!!(REG_F & 0x10))
#define FLAG_H (!!(REG_F & 0x20))
#define FLAG_N (!!(REG_F & 0x40))
#define FLAG_Z (!!(REG_F & 0x80))
#define SET_FLAG_C(n) do { REG_F ^= (-(!!(n)) ^ REG_F) & 0x10; } while(0)
#define SET_FLAG_H(n) do { REG_F ^= (-(!!(n)) ^ REG_F) & 0x20; } while(0)
#define SET_FLAG_N(n) do { REG_F ^= (-(!!(n)) ^ REG_F) & 0x40; } while(0)
#define SET_FLAG_Z(n) do { REG_F ^= (-(!!(n)) ^ REG_F) & 0x80; } while(0)
#define SET_FLAGS_HN(h,n) do { SET_FLAG_H(h); SET_FLAG_N(n); } while(0)
#define SET_FLAGS_HZ(h,z) do { SET_FLAG_H(h); SET_FLAG_Z(z); } while(0)
#define SET_FLAGS_HNZ(h,n,z) do { SET_FLAGS_HN(h,n); SET_FLAG_Z(z); } while(0)
#define SET_FLAGS_CHN(c,h,n) do { SET_FLAG_C(c); SET_FLAGS_HN(h,n); } while(0)
#define SET_ALL_FLAGS(c,h,n,z) do { SET_FLAGS_CHN(c,h,n); SET_FLAG_Z(z); } while(0)

#ifndef LR35902_READ16
static unsigned short LR35902_FAST_CODE LR35902_read16(void* user, unsigned short addr)
{
    unsigned short result = LR35902_read(user, addr + 0);
    return result |= LR35902_read(user, addr + 1) << 8;
}
#endif

#ifndef LR35902_WRITE16
static void LR35902_FAST_CODE LR35902_write16(void* user, unsigned short addr, unsigned short value)
{
    LR35902_write(user, addr + 0, value >> 0);
    LR35902_write(user, addr + 1, value >> 8);
}
#endif

#define read8(addr) LR35902_read(cpu->userdata, addr)
// #define read16(addr) (read8(addr) | (read8(addr + 1) << 8))
#define read16(addr) LR35902_read16(cpu->userdata, addr)
#define write8(addr, value) LR35902_write(cpu->userdata, addr, value)
// #define write16(addr, value) write8(addr, value & 0xFF); write8(addr + 1, (value >> 8) & 0xFF);
#define write16(addr, value) LR35902_write16(cpu->userdata, addr, value)

#ifndef LR35902_STACK_PUSH
static LR35902_FORCE_INLINE void LR35902_FAST_CODE LR35902_stack_push(void* user, unsigned short addr, unsigned short value) {
	LR35902_write16(user, addr, value);
}
#endif

#ifndef LR35902_STACK_POP
static LR35902_FORCE_INLINE unsigned short LR35902_FAST_CODE LR35902_stack_pop(void* user, unsigned short addr) {
	return LR35902_read16(user, addr);
}
#endif

static void LR35902_FAST_CODE _LR35902_push(struct LR35902* cpu, unsigned short value) {
	LR35902_stack_push(cpu->userdata, REG_SP - 2, value);
	REG_SP -= 2;
}

static unsigned short LR35902_FAST_CODE _LR35902_pop(struct LR35902* cpu) {
	const unsigned short result = LR35902_stack_pop(cpu->userdata, REG_SP);
	REG_SP += 2;
	return result;
}

#define PUSH(value) _LR35902_push(cpu, value)
#define POP() _LR35902_pop(cpu)

#define CALL() do { \
	const unsigned short result = read16(REG_PC); \
	PUSH(REG_PC + 2); \
	REG_PC = result; \
} while(0)
#define CALL_COND(cond) do { \
	if (cond) { \
		CALL(); \
		add_cycles(12); \
	} else { \
		REG_PC += 2; \
	} \
} while(0)
#define CALL_NZ() do { CALL_COND(!FLAG_Z); } while(0)
#define CALL_Z() do { CALL_COND(FLAG_Z); } while(0)
#define CALL_NC() do { CALL_COND(!FLAG_C); } while(0)
#define CALL_C() do { CALL_COND(FLAG_C); } while(0)

#define JP() do { REG_PC = read16(REG_PC); } while(0)
#define JP_HL() do { REG_PC = REG_HL; } while(0)
#define JP_COND(cond) do { \
	if (cond) { \
		JP(); \
		add_cycles(4); \
	} else { \
		REG_PC += 2; \
	} \
} while(0)
#define JP_NZ() do { JP_COND(!FLAG_Z); } while(0)
#define JP_Z() do { JP_COND(FLAG_Z); } while(0)
#define JP_NC() do { JP_COND(!FLAG_C); } while(0)
#define JP_C() do { JP_COND(FLAG_C); } while(0)

#define JR() do { REG_PC += ((signed char)read8(REG_PC)) + 1; } while(0)
#define JR_COND(cond) do { \
	if (cond) { \
		JR(); \
		add_cycles(4); \
	} else { \
		REG_PC++; \
	} \
} while(0)
#define JR_NZ() do { JR_COND(!FLAG_Z); } while(0)
#define JR_Z() do { JR_COND(FLAG_Z); } while(0)
#define JR_NC() do { JR_COND(!FLAG_C); } while(0)
#define JR_C() do { JR_COND(FLAG_C); } while(0)

#define RET() do { REG_PC = POP(); } while(0)
#define RET_COND(cond) do { \
	if (cond) { \
		RET(); \
		add_cycles(12); \
	} \
} while(0)
#define RET_NZ() do { RET_COND(!FLAG_Z); } while(0)
#define RET_Z() do { RET_COND(FLAG_Z); } while(0)
#define RET_NC() do { RET_COND(!FLAG_C); } while(0)
#define RET_C() do { RET_COND(FLAG_C); } while(0)

#define INC_r() do { \
	REG((opcode >> 3))++; \
	SET_FLAGS_HNZ(((REG((opcode >> 3)) & 0xF) == 0), 0, REG((opcode >> 3)) == 0); \
} while(0)

#define INC_HLa() do { \
	const unsigned char result = read8(REG_HL) + 1; \
	write8(REG_HL, result); \
	SET_FLAGS_HNZ((result & 0xF) == 0, 0, result == 0); \
} while (0)

#define DEC_r() do { \
	REG((opcode >> 3))--; \
	SET_FLAGS_HNZ(((REG((opcode >> 3)) & 0xF) == 0xF), 1, REG((opcode >> 3)) == 0); \
} while(0)

#define DEC_HLa() do { \
	const unsigned char result = read8(REG_HL) - 1; \
	write8(REG_HL, result); \
	SET_FLAGS_HNZ((result & 0xF) == 0xF, 1, result == 0); \
} while(0)

#define INC_BC() do { SET_REG_BC(REG_BC + 1); } while(0)
#define INC_DE() do { SET_REG_DE(REG_DE + 1); } while(0)
#define INC_HL() do { SET_REG_HL(REG_HL + 1); } while(0)
#define INC_SP() do { REG_SP++; } while(0)
#define DEC_BC() do { SET_REG_BC(REG_BC - 1); } while(0)
#define DEC_DE() do { SET_REG_DE(REG_DE - 1); } while(0)
#define DEC_HL() do { SET_REG_HL(REG_HL - 1); } while(0)
#define DEC_SP() do { REG_SP--; } while(0)

#define CP() do { \
	const unsigned char result = REG_A - REG(opcode); \
	SET_ALL_FLAGS(REG(opcode) < REG_A, (REG_A & 0xF) < (REG(opcode) & 0xF), 1, result == 0); \
} while(0)

#define LD_r_r() do { REG(opcode >> 3) = REG(opcode); } while(0)
#define LD_r_u8() do { REG(opcode >> 3) = read8(REG_PC++); } while(0)
#define LD_HLa_r() do { write8(REG_HL, REG(opcode)); } while(0)
#define LD_HLa_u8() do { write8(REG_HL, read8(REG_PC++)); } while(0)
#define LD_r_HLa() do { REG(opcode >> 3) = read8(REG_HL); } while(0)
#define LD_SP_u16() do { REG_SP = read16(REG_PC); REG_PC+=2; } while(0)
#define LD_A_u16() do { REG_A = read8(read16(REG_PC)); REG_PC+=2; } while(0)
#define LD_u16_A() do { write8(read16(REG_PC), REG_A); REG_PC+=2; } while(0)

#define LD_HLi_A() do { write8(REG_HL, REG_A); INC_HL(); } while(0)
#define LD_A_HLi() do { REG_A = read8(REG_HL); INC_HL(); } while(0)
#define LD_HLd_A() do { write8(REG_HL, REG_A); DEC_HL(); } while(0)
#define LD_A_HLd() do { REG_A = read8(REG_HL); DEC_HL(); } while(0)

#define LD_A_BCa() do { REG_A = read8(REG_BC); } while(0)
#define LD_A_DEa() do { REG_A = read8(REG_DE); } while(0)
#define LD_A_AFa() do { REG_A = read8(REG_AF); } while(0)
#define LD_BCa_A() do { write8(REG_BC, REG_A); } while(0)
#define LD_DEa_A() do { write8(REG_DE, REG_A); } while(0)
#define LD_AFa_A() do { write8(REG_AF, REG_A); } while(0)

#define LD_FFRC_A() do { write8(0xFF00 | REG_C, REG_A); } while(0)
#define LD_A_FFRC() do { REG_A = read8(0xFF00 | REG_C); } while(0)

#define LD_BC_u16() do { \
	const unsigned short result = read16(REG_PC); \
	SET_REG_BC(result); \
	REG_PC += 2; \
} while(0)
#define LD_DE_u16() do { \
	const unsigned short result = read16(REG_PC); \
	SET_REG_DE(result); \
	REG_PC += 2; \
} while(0)
#define LD_HL_u16() do { \
	const unsigned short result = read16(REG_PC); \
	SET_REG_HL(result); \
	REG_PC += 2; \
} while(0)

#define LD_SP_u16() do { REG_SP = read16(REG_PC); REG_PC+=2; } while(0)
#define LD_u16_BC() do { write16(read16(REG_PC), REG_BC); REG_PC+=2; } while(0)
#define LD_u16_DE() do { write16(read16(REG_PC), REG_DE); REG_PC+=2; } while(0)
#define LD_u16_HL() do { write16(read16(REG_PC), REG_HL); REG_PC+=2; } while(0)
#define LD_u16_SP() do { write16(read16(REG_PC), REG_SP); REG_PC+=2; } while(0)
#define LD_FFu8_A() do { write8((0xFF00 | read8(REG_PC++)), REG_A); } while(0)
#define LD_A_FFu8() do { REG_A = read8(0xFF00 | read8(REG_PC++)); } while(0)
#define LD_SP_HL() do { REG_SP = REG_HL; } while(0)

#define CP_r() do { \
	const unsigned char value = REG(opcode); \
	const unsigned char result = REG_A - value; \
	SET_ALL_FLAGS(value > REG_A, (REG_A & 0xF) < (value & 0xF), 1, result == 0); \
} while(0)

#define CP_u8() do { \
	const unsigned char value = read8(REG_PC++); \
	const unsigned char result = REG_A - value; \
	SET_ALL_FLAGS(value > REG_A, (REG_A & 0xF) < (value & 0xF), 1, result == 0); \
} while(0)

#define CP_HLa() do { \
	const unsigned char value = read8(REG_HL); \
	const unsigned char result = REG_A - value; \
	SET_ALL_FLAGS(value > REG_A, (REG_A & 0xF) < (value & 0xF), 1, result == 0); \
} while(0)

#define __ADD(value, carry) do { \
	const unsigned char result = REG_A + value + carry; \
    SET_ALL_FLAGS((REG_A + value + carry) > 0xFF, ((REG_A & 0xF) + (value & 0xF) + carry) > 0xF, 0, result == 0); \
    REG_A = result; \
} while (0)
#define ADD_r() do { __ADD(REG(opcode), 0); } while(0)
#define ADD_u8() do { const unsigned char value = read8(REG_PC++); __ADD(value, 0); } while(0)
#define ADD_HLa() do { const unsigned char value = read8(REG_HL); __ADD(value, 0); } while(0)
#define __ADD_HL(value) do { \
	const unsigned short result = REG_HL + value; \
	SET_FLAGS_CHN((REG_HL + value) > 0xFFFF, (REG_HL & 0xFFF) + (value & 0xFFF) > 0xFFF, 0); \
	SET_REG_HL(result); \
} while(0)

#define ADD_HL_BC() do { __ADD_HL(REG_BC); } while(0)
#define ADD_HL_DE() do { __ADD_HL(REG_DE); } while(0)
#define ADD_HL_HL() do { __ADD_HL(REG_HL); } while(0)
#define ADD_HL_SP() do { __ADD_HL(REG_SP); } while(0)
#define ADD_SP_i8() do { \
	const unsigned char value = read8(REG_PC++); \
    const unsigned short result = REG_SP + (signed char)value; \
    SET_ALL_FLAGS(((REG_SP & 0xFF) + value) > 0xFF, ((REG_SP & 0xF) + (value & 0xF)) > 0xF, 0, 0); \
	REG_SP = result; \
} while (0)

#define LD_HL_SP_i8() do { \
	const unsigned char value = read8(REG_PC++); \
    const unsigned short result = REG_SP + (signed char)value; \
    SET_ALL_FLAGS(((REG_SP & 0xFF) + value) > 0xFF, ((REG_SP & 0xF) + (value & 0xF)) > 0xF, 0, 0); \
	SET_REG_HL(result); \
} while (0)

#define ADC_r() do { \
	const unsigned char fc = FLAG_C; \
	__ADD(REG(opcode), fc); \
} while(0)

#define ADC_u8() do { \
	const unsigned char value = read8(REG_PC++); \
	const unsigned char fc = FLAG_C; \
	__ADD(value, fc); \
} while(0)

#define ADC_HLa() do { \
	const unsigned char value = read8(REG_HL); \
	const unsigned char fc = FLAG_C; \
	__ADD(value, fc); \
} while(0)

#define __SUB(value, carry) do { \
	const unsigned char result = REG_A - value - carry; \
	SET_ALL_FLAGS((value + carry) > REG_A, (REG_A & 0xF) < ((value & 0xF) + carry), 1, result == 0); \
    REG_A = result; \
} while (0)
#define SUB_r() do { __SUB(REG(opcode), 0); } while(0)
#define SUB_u8() do { const unsigned char value = read8(REG_PC++); __SUB(value, 0); } while(0)
#define SUB_HLa() do { const unsigned char value = read8(REG_HL); __SUB(value, 0); } while(0)
#define SBC_r() do { const unsigned char fc = FLAG_C; __SUB(REG(opcode), fc); } while(0)
#define SBC_u8() do { \
	const unsigned char value = read8(REG_PC++); \
	const unsigned char fc = FLAG_C; \
	__SUB(value, fc); \
} while(0)
#define SBC_HLa() do { \
	const unsigned char value = read8(REG_HL); \
	const unsigned char fc = FLAG_C; \
	__SUB(value, fc); \
} while(0)

#define AND_r() do { REG_A &= REG(opcode); SET_ALL_FLAGS(0, 1, 0, REG_A == 0); } while(0)
#define AND_u8() do { REG_A &= read8(REG_PC++); SET_ALL_FLAGS(0, 1, 0, REG_A == 0); } while(0)
#define AND_HLa() do { REG_A &= read8(REG_HL); SET_ALL_FLAGS(0, 1, 0, REG_A == 0); } while(0)

#define XOR_r() do { REG_A ^= REG(opcode); SET_ALL_FLAGS(0, 0, 0, REG_A == 0); } while(0)
#define XOR_u8() do { REG_A ^= read8(REG_PC++); SET_ALL_FLAGS(0, 0, 0, REG_A == 0); } while(0)
#define XOR_HLa() do { REG_A ^= read8(REG_HL); SET_ALL_FLAGS(0, 0, 0, REG_A == 0); } while(0)

#define OR_r() do { REG_A |= REG(opcode); SET_ALL_FLAGS(0, 0, 0, REG_A == 0); } while(0)
#define OR_u8() do { REG_A |= read8(REG_PC++); SET_ALL_FLAGS(0, 0, 0, REG_A == 0); } while(0)
#define OR_HLa() do { REG_A |= read8(REG_HL); SET_ALL_FLAGS(0, 0, 0, REG_A == 0); } while(0)

#define DI() do { cpu->IME = 0; } while(0)
#define EI() do { cpu->IME_delay = 1; } while(0)

#define POP_BC() do { const unsigned short result = POP(); SET_REG_BC(result); } while(0)
#define POP_DE() do { const unsigned short result = POP(); SET_REG_DE(result); } while(0)
#define POP_HL() do { const unsigned short result = POP(); SET_REG_HL(result); } while(0)
#define POP_AF() do { const unsigned short result = POP(); SET_REG_AF(result); } while(0)

#define RL_r() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) << 1) | (FLAG_C); \
	SET_ALL_FLAGS(value >> 7, 0, 0, REG(opcode) == 0); \
} while(0)

#define RLA() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) << 1) | (FLAG_C); \
	SET_ALL_FLAGS(value >> 7, 0, 0, 0); \
} while(0)

#define RL_HLa() do { \
	const unsigned char value = read8(REG_HL); \
	const unsigned char result = (value << 1) | (FLAG_C); \
	write8(REG_HL, result); \
	SET_ALL_FLAGS(value >> 7, 0, 0, result == 0); \
} while (0)

#define RLC_r() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) << 1) | ((REG(opcode) >> 7) & 1); \
	SET_ALL_FLAGS(value >> 7, 0, 0, REG(opcode) == 0); \
} while(0)

#define RLC_HLa() do { \
	const unsigned char value = read8(REG_HL); \
	const unsigned char result = (value << 1) | ((value >> 7) & 1); \
	write8(REG_HL, result); \
	SET_ALL_FLAGS(value >> 7, 0, 0, result == 0); \
} while(0)

#define RLCA() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) << 1) | ((REG(opcode) >> 7) & 1); \
	SET_ALL_FLAGS(value >> 7, 0, 0, 0); \
} while(0)

#define RR_r() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) >> 1) | (FLAG_C << 7); \
	SET_ALL_FLAGS(value & 1, 0, 0, REG(opcode) == 0); \
} while(0)

#define RR_HLa() do { \
	const unsigned char value = read8(REG_HL); \
	const unsigned char result = (value >> 1) | (FLAG_C << 7); \
	write8(REG_HL, result); \
	SET_ALL_FLAGS(value & 1, 0, 0, result == 0); \
} while(0)

#define RRA() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) >> 1) | (FLAG_C << 7); \
	SET_ALL_FLAGS(value & 1, 0, 0, 0); \
} while(0)

#define RRC_r() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) >> 1) | (REG(opcode) << 7); \
	SET_ALL_FLAGS(value & 1, 0, 0, REG(opcode) == 0); \
} while(0)

#define RRCA() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) >> 1) | (REG(opcode) << 7); \
	SET_ALL_FLAGS(value & 1, 0, 0, 0); \
} while(0)

#define RRC_HLa() do { \
	const unsigned char value = read8(REG_HL); \
    const unsigned char result = (value >> 1) | (value << 7); \
	write8(REG_HL, result); \
	SET_ALL_FLAGS(value & 1, 0, 0, result == 0); \
} while (0)

#define SLA_r() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) <<= 1; \
	SET_ALL_FLAGS(value >> 7, 0, 0, REG(opcode) == 0); \
} while(0)

#define SLA_HLa() do { \
	const unsigned char value = read8(REG_HL); \
    const unsigned char result = value << 1; \
	write8(REG_HL, result); \
	SET_ALL_FLAGS(value >> 7, 0, 0, result == 0); \
} while(0)

#define SRA_r() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) = (REG(opcode) >> 1) | (REG(opcode) & 0x80); \
	SET_ALL_FLAGS(value & 1, 0, 0, REG(opcode) == 0); \
} while(0)

#define SRA_HLa() do { \
	const unsigned char value = read8(REG_HL); \
    const unsigned char result = (value >> 1) | (value & 0x80); \
	write8(REG_HL, result); \
	SET_ALL_FLAGS(value & 1, 0, 0, result == 0); \
} while(0)

#define SRL_r() do { \
	const unsigned char value = REG(opcode); \
	REG(opcode) >>= 1; \
	SET_ALL_FLAGS(value & 1, 0, 0, REG(opcode) == 0); \
} while(0)

#define SRL_HLa() do { \
	const unsigned char value = read8(REG_HL); \
    const unsigned char result = (value >> 1); \
	write8(REG_HL, result); \
	SET_ALL_FLAGS(value & 1, 0, 0, result == 0); \
} while(0)

#define SWAP_r() do { \
    REG(opcode) = (REG(opcode) << 4) | (REG(opcode) >> 4); \
	SET_ALL_FLAGS(0, 0, 0, REG(opcode) == 0); \
} while(0)

#define SWAP_HLa() do { \
	const unsigned char value = read8(REG_HL); \
    const unsigned char result = (value << 4) | (value >> 4); \
	write8(REG_HL, result); \
	SET_ALL_FLAGS(0, 0, 0, result == 0); \
} while(0)

#define BIT_r() do { SET_FLAGS_HNZ(1, 0, (REG(opcode) & (1 << ((opcode >> 3) & 0x7))) == 0); } while(0)
#define BIT_HLa() do { SET_FLAGS_HNZ(1, 0, (read8(REG_HL) & (1 << ((opcode >> 3) & 0x7))) == 0); } while(0)

#define RES_r() do { REG(opcode) &= ~(1 << ((opcode >> 3) & 0x7)); } while(0)
#define RES_HLa() do { write8(REG_HL, (read8(REG_HL)) & ~(1 << ((opcode >> 3) & 0x7))); } while(0)

#define SET_r() do { REG(opcode) |= (1 << ((opcode >> 3) & 0x7)); } while(0)
#define SET_HLa() do { write8(REG_HL, (read8(REG_HL)) | (1 << ((opcode >> 3) & 0x7))); } while(0)

#define DAA() do { \
	if (FLAG_N) { \
        if (FLAG_C) { \
            REG_A -= 0x60; \
            SET_FLAG_C(1); \
        } \
        if (FLAG_H) { \
            REG_A -= 0x6; \
        } \
    } else { \
        if (FLAG_C || REG_A > 0x99) { \
            REG_A += 0x60; \
            SET_FLAG_C(1); \
        } \
        if (FLAG_H || (REG_A & 0x0F) > 0x09) { \
            REG_A += 0x6; \
        } \
    } \
	SET_FLAGS_HZ(0, REG_A == 0); \
} while(0)

#define RETI() do { REG_PC = POP(); EI(); } while(0)
#define RST(value) do { PUSH(REG_PC); REG_PC = value; } while(0)

#define CPL() do { REG_A = ~REG_A; SET_FLAGS_HN(1, 1); } while(0)
#define SCF() do { SET_FLAGS_CHN(1, 0, 0); } while(0)
#define CCF() do { SET_FLAGS_CHN(FLAG_C ^ 1, 0, 0); } while(0)
#define HALT() do { cpu->HALT = 1; } while(0)

#define STOP() do { \
	/* STOP is a 2-byte instruction, 0x10 | 0x00 */ \
    read8(REG_PC++); \
} while(0)

static LR35902_FORCE_INLINE void LR35902_FAST_CODE _LR35902_execute(struct LR35902* cpu);
static LR35902_FORCE_INLINE void LR35902_FAST_CODE _LR35902_execute_cb(struct LR35902* cpu);
static LR35902_FORCE_INLINE void LR35902_FAST_CODE _LR35902_interrupt_handler(struct LR35902* cpu);

static void LR35902_reset_common(struct LR35902* cpu) {
    cpu->IME = 0;
	cpu->HALT = 0;
#ifdef LR35902_BUILTIN_INTERRUTS
    cpu->IF = 0;
    cpu->IE = 0;
#endif
}

void LR35902_reset_dmg(struct LR35902* cpu) {
	SET_REG_AF(0x01B0);
	SET_REG_BC(0x0013);
	SET_REG_DE(0x00D8);
	SET_REG_HL(0x014D);
	REG_SP = 0xFFFE;
	REG_PC = 0x0100;
    LR35902_reset_common(cpu);
}

void LR35902_reset_cgb(struct LR35902* cpu) {
	SET_REG_AF(0x1180);
	SET_REG_BC(0x0000);
	SET_REG_DE(0xFF56);
	SET_REG_HL(0x000D);
	REG_SP = 0xFFFE;
	REG_PC = 0x0100;
    LR35902_reset_common(cpu);
}

void LR35902_reset_agb(struct LR35902* cpu) {
	SET_REG_AF(0x1100);
	SET_REG_BC(0x0100);
	SET_REG_DE(0xFF56);
	SET_REG_HL(0x000D);
	REG_SP = 0xFFFE;
	REG_PC = 0x0100;
    LR35902_reset_common(cpu);
}

void LR35902_reset_gbs(struct LR35902* cpu, unsigned short pc, unsigned short sp, unsigned char a) {
	SET_REG_AF(a << 8);
	SET_REG_BC(0x0000);
	SET_REG_DE(0x0000);
	SET_REG_HL(0x0000);
	REG_SP = sp;
	REG_PC = pc;
    LR35902_reset_common(cpu);
}

void LR35902_run(struct LR35902* cpu) {
	set_cycles(0);

	_LR35902_interrupt_handler(cpu);
	assert(!cpu->HALT && "LR35902_run() called whilst in halt mode!");

	// EI overlaps with the next fetch and ISR, meaning it hasn't yet
    // set ime during that time, hense the delay.
    // this is important as it means games can do:
    // EI -> ADD -> ISR, whereas without the delay, it would EI -> ISR.
    // this breaks bubble bobble if ime is not delayed!
    // SEE: https://github.com/ITotalJustice/TotalGB/issues/42
    cpu->IME |= cpu->IME_delay;
    cpu->IME_delay = false;

	// if (cpu->HALT) { /* this is slow, use gcc builtin likely */
	// 	cpu->cycles += 4;
	// 	return;
	// }
	_LR35902_execute(cpu);
}

#ifdef LR35902_BUILTIN_INTERRUTS
	#define LR35902_get_interrupts(a) cpu->IE & cpu->IF
	#define LR35902_handle_interrupt(a,i) cpu->IF &= ~(i)
#endif

static void _LR35902_interrupt_handler(struct LR35902* cpu) {
	unsigned char live_interrupts;

	if (!cpu->IME && !cpu->HALT) {
		return;
	}

	live_interrupts = LR35902_get_interrupts(cpu->userdata);
	if (!live_interrupts) {
		return;
	}

	cpu->HALT = 0;
	if (!cpu->IME) {
		assert(0);
		return;
	}
	cpu->IME = 0;

	if (live_interrupts & 0x01) { /* vblank */
        RST(64);
		LR35902_handle_interrupt(cpu->userdata, 0x01);
    } else if (live_interrupts & 0x02) { /* stat */
        RST(72);
		LR35902_handle_interrupt(cpu->userdata, 0x02);
    } else if (live_interrupts & 0x04) { /* timer */
        RST(80);
		LR35902_handle_interrupt(cpu->userdata, 0x04);
    } else if (live_interrupts & 0x08) { /* serial */
        RST(88);
		LR35902_handle_interrupt(cpu->userdata, 0x08);
    } else if (live_interrupts & 0x10) { /* joypad */
        RST(96);
		LR35902_handle_interrupt(cpu->userdata, 0x10);
    }

	add_cycles(20);
}

static void _LR35902_execute(struct LR35902* cpu) {
	register const unsigned char opcode = read8(REG_PC++);

	switch (opcode) {
	case 0x01: LD_BC_u16(); break;
	case 0x02: LD_BCa_A(); break;
	case 0x03: INC_BC(); break;
	case 0x07: RLCA(); break;
	case 0x08: LD_u16_SP(); break;
	case 0x09: ADD_HL_BC(); break;
	case 0x0A: LD_A_BCa(); break;
	case 0x0B: DEC_BC(); break;
	case 0x0F: RRCA(); break;
	case 0x10:
        STOP();
        #ifdef LR35902_ON_STOP
            LR35902_on_stop(cpu->userdata);
        #endif
        break;
	case 0x11: LD_DE_u16(); break;
	case 0x12: LD_DEa_A(); break;
	case 0x13: INC_DE(); break;
	case 0x17: RLA(); break;
	case 0x18: JR(); break;
	case 0x19: ADD_HL_DE(); break;
	case 0x1A: LD_A_DEa(); break;
	case 0x1B: DEC_DE(); break;
	case 0x1F: RRA(); break;
	case 0x20: JR_NZ(); break;
	case 0x21: LD_HL_u16(); break;
	case 0x22: LD_HLi_A(); break;
	case 0x23: INC_HL(); break;
	case 0x27: DAA(); break;
	case 0x28: JR_Z(); break;
	case 0x29: ADD_HL_HL(); break;
	case 0x2A: LD_A_HLi(); break;
	case 0x2B: DEC_HL(); break;
	case 0x06: /* FALLTHROUGH */
	case 0x0E: /* FALLTHROUGH */
	case 0x16: /* FALLTHROUGH */
	case 0x1E: /* FALLTHROUGH */
	case 0x26: /* FALLTHROUGH */
	case 0x2E: /* FALLTHROUGH */
	case 0x3E: LD_r_u8(); break;
	case 0x2F: CPL(); break;
	case 0x30: JR_NC(); break;
	case 0x31: LD_SP_u16(); break;
	case 0x32: LD_HLd_A(); break;
	case 0x33: INC_SP(); break;
	case 0x34: INC_HLa(); break;
	case 0x35: DEC_HLa(); break;
	case 0x36: LD_HLa_u8(); break;
	case 0x37: SCF(); break;
	case 0x38: JR_C(); break;
	case 0x39: ADD_HL_SP(); break;
	case 0x3A: LD_A_HLd(); break;
	case 0x3B: DEC_SP(); break;
	case 0x04: /* FALLTHROUGH */
	case 0x0C: /* FALLTHROUGH */
	case 0x14: /* FALLTHROUGH */
	case 0x1C: /* FALLTHROUGH */
	case 0x24: /* FALLTHROUGH */
	case 0x2C: /* FALLTHROUGH */
	case 0x3C: INC_r(); break;
	case 0x05: /* FALLTHROUGH */
	case 0x0D: /* FALLTHROUGH */
	case 0x15: /* FALLTHROUGH */
	case 0x1D: /* FALLTHROUGH */
	case 0x25: /* FALLTHROUGH */
	case 0x2D: /* FALLTHROUGH */
	case 0x3D: DEC_r(); break;
	case 0x3F: CCF(); break;
	case 0x00: /* NOP() */
	case 0x40: /* nop b,b */
	case 0x49: /* nop c,c */
	case 0x52: /* nop d,d */
	case 0x5B: /* nop e,e */
	case 0x64: /* nop h,h */
	case 0x6D: /* nop l,l */
	case 0x7F: /* nop a,a */ break;
	case 0x41: /* FALLTHROUGH */
	case 0x42: /* FALLTHROUGH */
	case 0x43: /* FALLTHROUGH */
	case 0x44: /* FALLTHROUGH */
	case 0x45: /* FALLTHROUGH */
	case 0x47: /* FALLTHROUGH */
	case 0x48: /* FALLTHROUGH */
	case 0x4A: /* FALLTHROUGH */
	case 0x4B: /* FALLTHROUGH */
	case 0x4C: /* FALLTHROUGH */
	case 0x4D: /* FALLTHROUGH */
	case 0x4F: /* FALLTHROUGH */
	case 0x50: /* FALLTHROUGH */
	case 0x51: /* FALLTHROUGH */
	case 0x53: /* FALLTHROUGH */
	case 0x54: /* FALLTHROUGH */
	case 0x55: /* FALLTHROUGH */
	case 0x57: /* FALLTHROUGH */
	case 0x58: /* FALLTHROUGH */
	case 0x59: /* FALLTHROUGH */
	case 0x5A: /* FALLTHROUGH */
	case 0x5C: /* FALLTHROUGH */
	case 0x5D: /* FALLTHROUGH */
	case 0x5F: /* FALLTHROUGH */
	case 0x60: /* FALLTHROUGH */
	case 0x61: /* FALLTHROUGH */
	case 0x62: /* FALLTHROUGH */
	case 0x63: /* FALLTHROUGH */
	case 0x65: /* FALLTHROUGH */
	case 0x67: /* FALLTHROUGH */
	case 0x68: /* FALLTHROUGH */
	case 0x69: /* FALLTHROUGH */
	case 0x6A: /* FALLTHROUGH */
	case 0x6B: /* FALLTHROUGH */
	case 0x6C: /* FALLTHROUGH */
	case 0x6F: /* FALLTHROUGH */
	case 0x78: /* FALLTHROUGH */
	case 0x79: /* FALLTHROUGH */
	case 0x7A: /* FALLTHROUGH */
	case 0x7B: /* FALLTHROUGH */
	case 0x7C: /* FALLTHROUGH */
	case 0x7D: LD_r_r(); break;
	case 0x46: /* FALLTHROUGH */
	case 0x4E: /* FALLTHROUGH */
	case 0x56: /* FALLTHROUGH */
	case 0x5E: /* FALLTHROUGH */
	case 0x66: /* FALLTHROUGH */
	case 0x6E: /* FALLTHROUGH */
	case 0x7E: LD_r_HLa(); break;
	case 0x76:
        HALT();
        #ifdef LR35902_ON_HALT
            LR35902_on_halt(cpu->userdata);
        #endif
        break;
	case 0x70: /* FALLTHROUGH */
	case 0x71: /* FALLTHROUGH */
	case 0x72: /* FALLTHROUGH */
	case 0x73: /* FALLTHROUGH */
	case 0x74: /* FALLTHROUGH */
	case 0x75: /* FALLTHROUGH */
	case 0x77: LD_HLa_r(); break;
	case 0x80: /* FALLTHROUGH */
	case 0x81: /* FALLTHROUGH */
	case 0x82: /* FALLTHROUGH */
	case 0x83: /* FALLTHROUGH */
	case 0x84: /* FALLTHROUGH */
	case 0x85: /* FALLTHROUGH */
	case 0x87: ADD_r(); break;
	case 0x86: ADD_HLa(); break;
	case 0x88: /* FALLTHROUGH */
	case 0x89: /* FALLTHROUGH */
	case 0x8A: /* FALLTHROUGH */
	case 0x8B: /* FALLTHROUGH */
	case 0x8C: /* FALLTHROUGH */
	case 0x8D: /* FALLTHROUGH */
	case 0x8F: ADC_r(); break;
	case 0x8E: ADC_HLa(); break;
	case 0x90: /* FALLTHROUGH */
	case 0x91: /* FALLTHROUGH */
	case 0x92: /* FALLTHROUGH */
	case 0x93: /* FALLTHROUGH */
	case 0x94: /* FALLTHROUGH */
	case 0x95: /* FALLTHROUGH */
	case 0x97: SUB_r(); break;
	case 0x96: SUB_HLa(); break;
	case 0x98: /* FALLTHROUGH */
	case 0x99: /* FALLTHROUGH */
	case 0x9A: /* FALLTHROUGH */
	case 0x9B: /* FALLTHROUGH */
	case 0x9C: /* FALLTHROUGH */
	case 0x9D: /* FALLTHROUGH */
	case 0x9F: SBC_r(); break;
	case 0x9E: SBC_HLa(); break;
	case 0xA0: /* FALLTHROUGH */
	case 0xA1: /* FALLTHROUGH */
	case 0xA2: /* FALLTHROUGH */
	case 0xA3: /* FALLTHROUGH */
	case 0xA4: /* FALLTHROUGH */
	case 0xA5: /* FALLTHROUGH */
	case 0xA7: AND_r(); break;
	case 0xA6: AND_HLa(); break;
	case 0xA8: /* FALLTHROUGH */
	case 0xA9: /* FALLTHROUGH */
	case 0xAA: /* FALLTHROUGH */
	case 0xAB: /* FALLTHROUGH */
	case 0xAC: /* FALLTHROUGH */
	case 0xAD: /* FALLTHROUGH */
	case 0xAF: XOR_r(); break;
	case 0xAE: XOR_HLa(); break;
	case 0xB0: /* FALLTHROUGH */
	case 0xB1: /* FALLTHROUGH */
	case 0xB2: /* FALLTHROUGH */
	case 0xB3: /* FALLTHROUGH */
	case 0xB4: /* FALLTHROUGH */
	case 0xB5: /* FALLTHROUGH */
	case 0xB7: OR_r(); break;
	case 0xB6: OR_HLa(); break;
	case 0xB8: /* FALLTHROUGH */
	case 0xB9: /* FALLTHROUGH */
	case 0xBA: /* FALLTHROUGH */
	case 0xBB: /* FALLTHROUGH */
	case 0xBC: /* FALLTHROUGH */
	case 0xBD: /* FALLTHROUGH */
	case 0xBF: CP_r(); break;
	case 0xBE: CP_HLa(); break;
	case 0xC0: RET_NZ(); break;
	case 0xC1: POP_BC(); break;
	case 0xC2: JP_NZ(); break;
	case 0xC3: JP();  break;
	case 0xC4: CALL_NZ(); break;
	case 0xC5: PUSH(REG_BC); break;
	case 0xC6: ADD_u8(); break;
	case 0xC7: RST(0x00); break;
	case 0xC8: RET_Z(); break;
	case 0xC9: RET(); break;
	case 0xCA: JP_Z(); break;
	case 0xCB: _LR35902_execute_cb(cpu); break;
	case 0xCC: CALL_Z(); break;
	case 0xCD: CALL(); break;
	case 0xCE: ADC_u8(); break;
	case 0xCF: RST(0x08); break;
	case 0xD0: RET_NC(); break;
	case 0xD1: POP_DE();  break;
	case 0xD2: JP_NC(); break;
	case 0xD4: CALL_NC(); break;
	case 0xD5: PUSH(REG_DE); break;
	case 0xD6: SUB_u8(); break;
	case 0xD7: RST(0x10); break;
	case 0xD8: RET_C(); break;
	case 0xD9: RETI(); break;
	case 0xDA: JP_C(); break;
	case 0xDC: CALL_C(); break;
	case 0xDE: SBC_u8(); break;
	case 0xDF: RST(0x18); break;
	case 0xE0: LD_FFu8_A(); break;
	case 0xE1: POP_HL(); break;
	case 0xE2: LD_FFRC_A(); break;
	case 0xE5: PUSH(REG_HL); break;
	case 0xE6: AND_u8(); break;
	case 0xE7: RST(0x20); break;
	case 0xE8: ADD_SP_i8(); break;
	case 0xE9: JP_HL(); break;
	case 0xEA: LD_u16_A(); break;
	case 0xEE: XOR_u8(); break;
	case 0xEF: RST(0x28); break;
	case 0xF0: LD_A_FFu8(); break;
	case 0xF1: POP_AF(); break;
	case 0xF2: LD_A_FFRC(); break;
	case 0xF3: DI(); break;
	case 0xF5: PUSH(REG_AF); break;
	case 0xF6: OR_u8(); break;
	case 0xF7: RST(0x30); break;
	case 0xF8: LD_HL_SP_i8(); break;
	case 0xF9: LD_SP_HL(); break;
	case 0xFA: LD_A_u16(); break;
	case 0xFB: EI(); break;
	case 0xFE: CP_u8(); break;
	case 0xFF: RST(0x38); break;
	}

	add_cycles(CYCLE_TABLE[opcode]);
}

// old: 384.4 KiB (393,584)
// ncb: 380.0 KiB (389,144)
// new: 378.9 KiB (387,976)
// ne2: 378.3 KiB (387,336)

static void _LR35902_execute_cb(struct LR35902* cpu) {
	register const unsigned char opcode = read8(REG_PC++);

	switch (opcode) {
	case 0x00: /* FALLTHROUGH */
	case 0x01: /* FALLTHROUGH */
	case 0x02: /* FALLTHROUGH */
	case 0x03: /* FALLTHROUGH */
	case 0x04: /* FALLTHROUGH */
	case 0x05: /* FALLTHROUGH */
	case 0x07: RLC_r(); break;
	case 0x06: RLC_HLa(); break;
	case 0x08: /* FALLTHROUGH */
	case 0x09: /* FALLTHROUGH */
	case 0x0A: /* FALLTHROUGH */
	case 0x0B: /* FALLTHROUGH */
	case 0x0C: /* FALLTHROUGH */
	case 0x0D: /* FALLTHROUGH */
	case 0x0F: RRC_r(); break;
	case 0x0E: RRC_HLa(); break;
	case 0x10: /* FALLTHROUGH */
	case 0x11: /* FALLTHROUGH */
	case 0x12: /* FALLTHROUGH */
	case 0x13: /* FALLTHROUGH */
	case 0x14: /* FALLTHROUGH */
	case 0x15: /* FALLTHROUGH */
	case 0x17: RL_r(); break;
	case 0x16: RL_HLa(); break;
	case 0x18: /* FALLTHROUGH */
	case 0x19: /* FALLTHROUGH */
	case 0x1A: /* FALLTHROUGH */
	case 0x1B: /* FALLTHROUGH */
	case 0x1C: /* FALLTHROUGH */
	case 0x1D: /* FALLTHROUGH */
	case 0x1F: RR_r(); break;
	case 0x1E: RR_HLa(); break;
	case 0x20: /* FALLTHROUGH */
	case 0x21: /* FALLTHROUGH */
	case 0x22: /* FALLTHROUGH */
	case 0x23: /* FALLTHROUGH */
	case 0x24: /* FALLTHROUGH */
	case 0x25: /* FALLTHROUGH */
	case 0x27: SLA_r(); break;
	case 0x26: SLA_HLa(); break;
	case 0x28: /* FALLTHROUGH */
	case 0x29: /* FALLTHROUGH */
	case 0x2A: /* FALLTHROUGH */
	case 0x2B: /* FALLTHROUGH */
	case 0x2C: /* FALLTHROUGH */
	case 0x2D: /* FALLTHROUGH */
	case 0x2F: SRA_r(); break;
	case 0x2E: SRA_HLa(); break;
	case 0x30: /* FALLTHROUGH */
	case 0x31: /* FALLTHROUGH */
	case 0x32: /* FALLTHROUGH */
	case 0x33: /* FALLTHROUGH */
	case 0x34: /* FALLTHROUGH */
	case 0x35: /* FALLTHROUGH */
	case 0x37: SWAP_r(); break;
	case 0x36: SWAP_HLa(); break;
	case 0x38: /* FALLTHROUGH */
	case 0x39: /* FALLTHROUGH */
	case 0x3A: /* FALLTHROUGH */
	case 0x3B: /* FALLTHROUGH */
	case 0x3C: /* FALLTHROUGH */
	case 0x3D: /* FALLTHROUGH */
	case 0x3F: SRL_r(); break;
	case 0x3E: SRL_HLa(); break;
	case 0x40: /* FALLTHROUGH */
	case 0x41: /* FALLTHROUGH */
	case 0x42: /* FALLTHROUGH */
	case 0x43: /* FALLTHROUGH */
	case 0x44: /* FALLTHROUGH */
	case 0x45: /* FALLTHROUGH */
	case 0x47: /* FALLTHROUGH */
	case 0x48: /* FALLTHROUGH */
	case 0x49: /* FALLTHROUGH */
	case 0x4A: /* FALLTHROUGH */
	case 0x4B: /* FALLTHROUGH */
	case 0x4C: /* FALLTHROUGH */
	case 0x4D: /* FALLTHROUGH */
	case 0x4F: /* FALLTHROUGH */
	case 0x50: /* FALLTHROUGH */
	case 0x51: /* FALLTHROUGH */
	case 0x52: /* FALLTHROUGH */
	case 0x53: /* FALLTHROUGH */
	case 0x54: /* FALLTHROUGH */
	case 0x55: /* FALLTHROUGH */
	case 0x57: /* FALLTHROUGH */
	case 0x58: /* FALLTHROUGH */
	case 0x59: /* FALLTHROUGH */
	case 0x5A: /* FALLTHROUGH */
	case 0x5B: /* FALLTHROUGH */
	case 0x5C: /* FALLTHROUGH */
	case 0x5D: /* FALLTHROUGH */
	case 0x5F: /* FALLTHROUGH */
	case 0x60: /* FALLTHROUGH */
	case 0x61: /* FALLTHROUGH */
	case 0x62: /* FALLTHROUGH */
	case 0x63: /* FALLTHROUGH */
	case 0x64: /* FALLTHROUGH */
	case 0x65: /* FALLTHROUGH */
	case 0x67: /* FALLTHROUGH */
	case 0x68: /* FALLTHROUGH */
	case 0x69: /* FALLTHROUGH */
	case 0x6A: /* FALLTHROUGH */
	case 0x6B: /* FALLTHROUGH */
	case 0x6C: /* FALLTHROUGH */
	case 0x6D: /* FALLTHROUGH */
	case 0x6F: /* FALLTHROUGH */
	case 0x70: /* FALLTHROUGH */
	case 0x71: /* FALLTHROUGH */
	case 0x72: /* FALLTHROUGH */
	case 0x73: /* FALLTHROUGH */
	case 0x74: /* FALLTHROUGH */
	case 0x75: /* FALLTHROUGH */
	case 0x77: /* FALLTHROUGH */
	case 0x78: /* FALLTHROUGH */
	case 0x79: /* FALLTHROUGH */
	case 0x7A: /* FALLTHROUGH */
	case 0x7B: /* FALLTHROUGH */
	case 0x7C: /* FALLTHROUGH */
	case 0x7D: /* FALLTHROUGH */
	case 0x7F: BIT_r(); break;
	case 0x46: /* FALLTHROUGH */
	case 0x4E: /* FALLTHROUGH */
	case 0x56: /* FALLTHROUGH */
	case 0x5E: /* FALLTHROUGH */
	case 0x66: /* FALLTHROUGH */
	case 0x6E: /* FALLTHROUGH */
	case 0x76: /* FALLTHROUGH */
	case 0x7E: BIT_HLa(); break;
	case 0x80: /* FALLTHROUGH */
	case 0x81: /* FALLTHROUGH */
	case 0x82: /* FALLTHROUGH */
	case 0x83: /* FALLTHROUGH */
	case 0x84: /* FALLTHROUGH */
	case 0x85: /* FALLTHROUGH */
	case 0x87: /* FALLTHROUGH */
	case 0x88: /* FALLTHROUGH */
	case 0x89: /* FALLTHROUGH */
	case 0x8A: /* FALLTHROUGH */
	case 0x8B: /* FALLTHROUGH */
	case 0x8C: /* FALLTHROUGH */
	case 0x8D: /* FALLTHROUGH */
	case 0x8F: /* FALLTHROUGH */
	case 0x90: /* FALLTHROUGH */
	case 0x91: /* FALLTHROUGH */
	case 0x92: /* FALLTHROUGH */
	case 0x93: /* FALLTHROUGH */
	case 0x94: /* FALLTHROUGH */
	case 0x95: /* FALLTHROUGH */
	case 0x97: /* FALLTHROUGH */
	case 0x98: /* FALLTHROUGH */
	case 0x99: /* FALLTHROUGH */
	case 0x9A: /* FALLTHROUGH */
	case 0x9B: /* FALLTHROUGH */
	case 0x9C: /* FALLTHROUGH */
	case 0x9D: /* FALLTHROUGH */
	case 0x9F: /* FALLTHROUGH */
	case 0xA0: /* FALLTHROUGH */
	case 0xA1: /* FALLTHROUGH */
	case 0xA2: /* FALLTHROUGH */
	case 0xA3: /* FALLTHROUGH */
	case 0xA4: /* FALLTHROUGH */
	case 0xA5: /* FALLTHROUGH */
	case 0xA7: /* FALLTHROUGH */
	case 0xA8: /* FALLTHROUGH */
	case 0xA9: /* FALLTHROUGH */
	case 0xAA: /* FALLTHROUGH */
	case 0xAB: /* FALLTHROUGH */
	case 0xAC: /* FALLTHROUGH */
	case 0xAD: /* FALLTHROUGH */
	case 0xAF: /* FALLTHROUGH */
	case 0xB0: /* FALLTHROUGH */
	case 0xB1: /* FALLTHROUGH */
	case 0xB2: /* FALLTHROUGH */
	case 0xB3: /* FALLTHROUGH */
	case 0xB4: /* FALLTHROUGH */
	case 0xB5: /* FALLTHROUGH */
	case 0xB7: /* FALLTHROUGH */
	case 0xB8: /* FALLTHROUGH */
	case 0xB9: /* FALLTHROUGH */
	case 0xBA: /* FALLTHROUGH */
	case 0xBB: /* FALLTHROUGH */
	case 0xBC: /* FALLTHROUGH */
	case 0xBD: /* FALLTHROUGH */
	case 0xBF: RES_r(); break;
	case 0x86: /* FALLTHROUGH */
	case 0x8E: /* FALLTHROUGH */
	case 0x96: /* FALLTHROUGH */
	case 0x9E: /* FALLTHROUGH */
	case 0xA6: /* FALLTHROUGH */
	case 0xAE: /* FALLTHROUGH */
	case 0xB6: /* FALLTHROUGH */
	case 0xBE: RES_HLa(); break;
	case 0xC0: /* FALLTHROUGH */
	case 0xC1: /* FALLTHROUGH */
	case 0xC2: /* FALLTHROUGH */
	case 0xC3: /* FALLTHROUGH */
	case 0xC4: /* FALLTHROUGH */
	case 0xC5: /* FALLTHROUGH */
	case 0xC7: /* FALLTHROUGH */
	case 0xC8: /* FALLTHROUGH */
	case 0xC9: /* FALLTHROUGH */
	case 0xCA: /* FALLTHROUGH */
	case 0xCB: /* FALLTHROUGH */
	case 0xCC: /* FALLTHROUGH */
	case 0xCD: /* FALLTHROUGH */
	case 0xCF: /* FALLTHROUGH */
	case 0xD0: /* FALLTHROUGH */
	case 0xD1: /* FALLTHROUGH */
	case 0xD2: /* FALLTHROUGH */
	case 0xD3: /* FALLTHROUGH */
	case 0xD4: /* FALLTHROUGH */
	case 0xD5: /* FALLTHROUGH */
	case 0xD7: /* FALLTHROUGH */
	case 0xD8: /* FALLTHROUGH */
	case 0xD9: /* FALLTHROUGH */
	case 0xDA: /* FALLTHROUGH */
	case 0xDB: /* FALLTHROUGH */
	case 0xDC: /* FALLTHROUGH */
	case 0xDD: /* FALLTHROUGH */
	case 0xDF: /* FALLTHROUGH */
	case 0xE0: /* FALLTHROUGH */
	case 0xE1: /* FALLTHROUGH */
	case 0xE2: /* FALLTHROUGH */
	case 0xE3: /* FALLTHROUGH */
	case 0xE4: /* FALLTHROUGH */
	case 0xE5: /* FALLTHROUGH */
	case 0xE7: /* FALLTHROUGH */
	case 0xE8: /* FALLTHROUGH */
	case 0xE9: /* FALLTHROUGH */
	case 0xEA: /* FALLTHROUGH */
	case 0xEB: /* FALLTHROUGH */
	case 0xEC: /* FALLTHROUGH */
	case 0xED: /* FALLTHROUGH */
	case 0xEF: /* FALLTHROUGH */
	case 0xF0: /* FALLTHROUGH */
	case 0xF1: /* FALLTHROUGH */
	case 0xF2: /* FALLTHROUGH */
	case 0xF3: /* FALLTHROUGH */
	case 0xF4: /* FALLTHROUGH */
	case 0xF5: /* FALLTHROUGH */
	case 0xF7: /* FALLTHROUGH */
	case 0xF8: /* FALLTHROUGH */
	case 0xF9: /* FALLTHROUGH */
	case 0xFA: /* FALLTHROUGH */
	case 0xFB: /* FALLTHROUGH */
	case 0xFC: /* FALLTHROUGH */
	case 0xFD: /* FALLTHROUGH */
	case 0xFF: SET_r(); break;
	case 0xC6: /* FALLTHROUGH */
	case 0xCE: /* FALLTHROUGH */
	case 0xD6: /* FALLTHROUGH */
	case 0xDE: /* FALLTHROUGH */
	case 0xE6: /* FALLTHROUGH */
	case 0xEE: /* FALLTHROUGH */
	case 0xF6: /* FALLTHROUGH */
	case 0xFE: SET_HLa(); break;
	}

	add_cycles(CYCLE_TABLE_CB[opcode]);
}

#endif /* LR35902_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* LR35902 */
