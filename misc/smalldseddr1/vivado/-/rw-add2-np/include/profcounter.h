#ifndef PROFCOUNTER_H
#define PROFCOUNTER_H

/* Source-end of AXI4-Stream that goes to profCounter */
__write_only pipe unsigned p0 __attribute__((xcl_reqd_pipe_depth(16)));

/* Command macros */
#define __PROFCOUNTER_COMM_CHECKPOINT_0__ 0x1
#define __PROFCOUNTER_COMM_CHECKPOINT_1__ 0x2
#define __PROFCOUNTER_COMM_CHECKPOINT_2__ 0x3
#define __PROFCOUNTER_COMM_CHECKPOINT_3__ 0x4
#define __PROFCOUNTER_COMM_CHECKPOINT_4__ 0x5
#define __PROFCOUNTER_COMM_CHECKPOINT_5__ 0x6
#define __PROFCOUNTER_COMM_CHECKPOINT_6__ 0x7
#define __PROFCOUNTER_COMM_CHECKPOINT_7__ 0x8
#define __PROFCOUNTER_COMM_CHECKPOINT_8__ 0x9
#define __PROFCOUNTER_COMM_CHECKPOINT_9__ 0xA
#define __PROFCOUNTER_COMM_CHECKPOINT_10__ 0xB
#define __PROFCOUNTER_COMM_CHECKPOINT_11__ 0xC
#define __PROFCOUNTER_COMM_STAMP__ 0xD
#define __PROFCOUNTER_COMM_HOLD__ 0xE
#define __PROFCOUNTER_COMM_FINISH__ 0xF

/**
 * Placeholder dummy variable. All PROFCOUNTER_* calls apart from PROFCOUNTER_FINISH() makes use of this variable.
 * Just before scheduling/binding, this variable is removed and the operations performed in it are substituted by the actual write_pipe() calls.
 * This avoids, the optimiser of generating a different CFG just because of the presence of pipe.
 */
#define PROFCOUNTER_INIT() __private volatile unsigned __PROFCOUNTER_COMM_DUMMY_VAR__ = 0xDEADBEEF;

/* Request profcounter to hold all writes to global memory until PROFCOUNTER_FINISH() is called */
#define PROFCOUNTER_HOLD() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_HOLD__

/* Issue a checkpoint command, with the ID defined at compile-time */
#define PROFCOUNTER_CHECKPOINT_0() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_0__
#define PROFCOUNTER_CHECKPOINT_1() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_1__
#define PROFCOUNTER_CHECKPOINT_2() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_2__
#define PROFCOUNTER_CHECKPOINT_3() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_3__
#define PROFCOUNTER_CHECKPOINT_4() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_4__
#define PROFCOUNTER_CHECKPOINT_5() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_5__
#define PROFCOUNTER_CHECKPOINT_6() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_6__
#define PROFCOUNTER_CHECKPOINT_7() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_7__
#define PROFCOUNTER_CHECKPOINT_8() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_8__
#define PROFCOUNTER_CHECKPOINT_9() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_9__
#define PROFCOUNTER_CHECKPOINT_10() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_10__
#define PROFCOUNTER_CHECKPOINT_11() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_CHECKPOINT_11__

/* Issue a stamp command */
#define PROFCOUNTER_STAMP() __PROFCOUNTER_COMM_DUMMY_VAR__ += __PROFCOUNTER_COMM_STAMP__

/**
 * Finish kernel execution and release hold if applicable.
 * This is the only macro that actually calls write_pipe() before optimisation. This is necessary, otherwise the optimiser will optimise away
 * the OpenCL pipe.
 */
#define PROFCOUNTER_FINISH() write_pipe(p0, &(unsigned){__PROFCOUNTER_COMM_FINISH__})

#endif