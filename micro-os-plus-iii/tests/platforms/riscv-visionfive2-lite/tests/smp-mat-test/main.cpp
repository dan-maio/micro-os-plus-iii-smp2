/*
 * The Cortex-M Pico 2 test smp-mat-test (micro-os-plus-iii-cortexm,
 * test/pico2/smp-mat-test), through its RISC-V Pico 2 port, on the
 * VisionFive 2 Lite's four U74 harts (mhartid 1..4 = CPUs 0..3, OS_NCPU = 4):
 * CPUs 1..3 released from the port's park (multicore::launch_core) with
 * their idle threads idle1..idle3, the RP2350 TIMER0 replaced by the CLINT
 * mtime (4 MHz), the barrier's SIO hardware spinlock by an AMO spinlock, and
 * N = 240, B = 30: 8 row blocks, two per core (the RP2350B's N = 300,
 * B = 50 gives 6). Built for 1 CPU it is the single-hart run.
 */
/*
 * smp-mat-test (Pico 2 / RP2350) — parallel block linear equation solver,
 * the pico2 port of the A7 `smp_test4` (N = 100, B = 50, 2 cores).
 *
 * Solves A x = b (N x N, diagonally dominant) three ways:
 *   1. a Thread-Pool demo (10 workers, 20 tasks) proving cross-core task
 *      distribution through a semaphore+mutex pool: worker i runs on CPU
 *      i % OS_NCPU, and every core must execute at least one task;
 *   2. a PARALLEL BLOCK elimination (the real test): block Gaussian
 *      elimination with block size B, row blocks distributed across the two
 *      cores, synchronized by generation-based spin BARRIERS, with core 0
 *      doing the diagonal LU + back substitution;
 *   3. a CLASSICAL N x N LU solve on core 0, as ground truth.
 * Then it compares the two solutions (max/sum absolute difference), times
 * both, and prints the speedup.
 *
 * pico2 adaptations vs the A7 original:
 *   - the A7's Barrier used __sync_lock_test_and_set (LDREX/STREX), which is
 *     ONLY per-core exclusive on the dual-core RP2350 -> replaced with the SIO
 *     hardware spinlock (slots 2/3; 0 = kernel, 1 = console);
 *   - the ARM Physical Counter (CNTPCT) -> RP2350 TIMER0 microsecond counter;
 *   - core 1 is launched by the bootrom FIFO (multicore::launch_core1) instead
 *     of the A7's smp::start_secondary_cores;
 *   - UART/LED via the pico2 BSP (snprintf helpers; no float printf in the
 *     nano library).
 *
 * Output on UART0 @115200; the LED blinks while the solver runs and becomes a
 * heartbeat after the summary.
 */

#include <cmsis-plus/rtos/os.h>

#include <led.hpp>
#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>
#include <hw_result.hpp>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>

#include <clint.hpp>
#include <cstdio>
#include <cstdlib>

using namespace os::rtos;

#if !defined(OS_NCPU)
#define OS_NCPU 1
#endif

#if OS_NCPU > 1
#include <multicore.hpp>
#include <riscv-smp.hpp>

namespace os
{
  namespace rtos
  {
    namespace scheduler
    {
      extern thread* os_idle_thread_core[OS_NCPU];
    }
  }
}
#endif

// This CPU (the kernel's index) and the hart's own mhartid.
static unsigned
current_cpu (void)
{
#if OS_NCPU > 1
  return port_cpu_id ();
#else
  return 0;
#endif
}

static unsigned
current_mhartid (void)
{
  return static_cast<unsigned> (RISCV_CSR_READ (mhartid));
}

// ----------------------------------------------------------------------------
// Time: clint::now () (riscv_board::TIMEBASE_HZ), in microseconds.
// ----------------------------------------------------------------------------
namespace
{
  void
  timer_init (void)
  {
  }

  inline std::uint32_t
  timer_us32 (void)
  {
    return static_cast<std::uint32_t> (clint::now ()
                                       / (riscv_board::TIMEBASE_HZ / 1000000u));
  }

  // ---- Console helpers (snprintf; the nano lib has no float printf) --------
  void
  write_str (const char* s)
  {
    uart::write (s);
  }

  void
  led_toggle (void)
  {
    static bool on = false;
    on = !on;
    if (on)
      {
        led::on ();
      }
    else
      {
        led::off ();
      }
  }

  void
  write_fmt (const char* fmt, ...)
  {
    char b[96];
    std::va_list args;
    va_start (args, fmt);
    int n = std::vsnprintf (b, sizeof (b), fmt, args);
    va_end (args);
    if (n < 0)
      {
        n = 0;
      }
    if (n > static_cast<int> (sizeof (b)) - 1)
      {
        n = static_cast<int> (sizeof (b)) - 1;
      }
    uart::write (b, static_cast<std::size_t> (n));
  }

  void
  print_float (float val)
  {
    char b[32];
    if (val < 0.0f)
      {
        uart::write ("-");
        val = -val;
      }
    int int_part = static_cast<int> (val);
    int frac_part
        = static_cast<int> ((val - int_part) * 1000000.0f + 0.5f);
    if (frac_part >= 1000000)
      {
        int_part = int_part + 1;
        frac_part = frac_part - 1000000;
      }
    int n = std::snprintf (b, sizeof (b), "%d.%06d", int_part, frac_part);
    uart::write (b, static_cast<std::size_t> (n));
  }

  void
  print_time_us (double time_us)
  {
    char b[32];
    int int_part = static_cast<int> (time_us);
    int frac_part
        = static_cast<int> ((time_us - int_part) * 1000.0 + 0.5);
    if (frac_part >= 1000)
      {
        int_part = int_part + 1;
        frac_part = frac_part - 1000;
      }
    int n = std::snprintf (b, sizeof (b), "%d.%03d us", int_part, frac_part);
    uart::write (b, static_cast<std::size_t> (n));
  }

  void
  print_ratio (double ratio)
  {
    char b[32];
    int int_part = static_cast<int> (ratio);
    int frac_part
        = static_cast<int> ((ratio - int_part) * 1000.0 + 0.5);
    if (frac_part >= 1000)
      {
        int_part = int_part + 1;
        frac_part = frac_part - 1000;
      }
    int n = std::snprintf (b, sizeof (b), "%d.%03d", int_part, frac_part);
    uart::write (b, static_cast<std::size_t> (n));
  }
}

// ----------------------------------------------------------------------------
// Solver dimensions.  The board's tests.cmake may override them; this VF2
// build uses N = 320, B = 40: 8 row blocks, two per U74 core.
// ----------------------------------------------------------------------------
#ifndef SMP_MAT_N
#define SMP_MAT_N 240
#endif
#ifndef SMP_MAT_B
#define SMP_MAT_B 30
#endif
#define N SMP_MAT_N
#define B SMP_MAT_B

// ----------------------------------------------------------------------------
// Generation-based spin barrier, protected by an AMO spinlock.
// ----------------------------------------------------------------------------
class Barrier
{
public:
  void
  init (int count, std::uint32_t sio_slot)
  {
    target_count = count;
    current_count = 0;
    generation = 0;
    slot_ = sio_slot;
  }

  void
  wait (void)
  {
    __asm__ volatile ("fence rw, rw" ::: "memory");
    int gen = generation;

    // Take the barrier's spinlock: an AMO (amoswap.aq). The RP2350 version
    // used an SIO hardware spinlock (its LDREX/STREX were per-core only).
    while (__atomic_exchange_n (&lock_, 1u, __ATOMIC_ACQUIRE) != 0u)
      {
        this_thread::yield ();
      }

    current_count = current_count + 1;
    if (current_count == target_count)
      {
        current_count = 0;
        generation = generation + 1;
        __atomic_store_n (&lock_, 0u, __ATOMIC_RELEASE); // release
      }
    else
      {
        __atomic_store_n (&lock_, 0u, __ATOMIC_RELEASE); // release
        while (generation == gen)
          {
            this_thread::yield ();
          }
      }

    __asm__ volatile ("fence rw, rw" ::: "memory");
  }

private:
  int target_count;
  volatile int current_count;
  volatile int generation;
  std::uint32_t slot_;
  std::uint32_t lock_ = 0; // AMO spinlock (the RP2350 used an SIO one)
};

// ----------------------------------------------------------------------------
// Thread Pool (10 workers, cross-core tasks)
// ----------------------------------------------------------------------------
typedef void (*task_t) (void* arg);

struct Task
{
  task_t func;
  void* arg;
};

#define POOL_SIZE 10
#define TASK_QUEUE_SIZE 32

class ThreadPool
{
public:
  ThreadPool ()
      : sem_tasks_ ("pool_tasks", TASK_QUEUE_SIZE, 0),
        sem_free_ ("pool_free", TASK_QUEUE_SIZE, TASK_QUEUE_SIZE)
  {
    head_ = 0;
    tail_ = 0;
    count_ = 0;
    ready_ = 0;
  }

  void start ();
  bool submit (task_t func, void* arg);
  void worker_loop ();

  // Called by a worker once it is actually running on its own core. The
  // demo waits for all of them before submitting work; otherwise the CPU-0
  // workers drain the queue before the workers pinned to the other cores
  // have had a chance to be scheduled (measured in an Eclipse session:
  // 20 tasks on CPU 0, 0 on the others, while the solver later ran on all
  // four harts).
  void
  worker_ready ()
  {
    __atomic_add_fetch (&ready_, 1u, __ATOMIC_RELEASE);
  }

private:
  Task queue_[TASK_QUEUE_SIZE];
  int head_;
  int tail_;
  int count_;
  volatile unsigned ready_;

  mutex mutex_;
  semaphore_counting sem_tasks_;
  semaphore_counting sem_free_;
};

static ThreadPool g_pool;
static char pool_thread_names[POOL_SIZE][16];
static thread::stack::element_t pool_stacks[POOL_SIZE][512];
static thread* pool_threads[POOL_SIZE];
static int pool_worker_index[POOL_SIZE];

static void*
pool_worker_func (void* arg)
{
  const int i = *static_cast<int*> (arg);
#if OS_NCPU > 1
  // Pin first, then get there, as the solvers do: a new thread runs on the
  // core that created it (CPU 0, os_main) and stays there -- the affinity
  // ThreadPool::start() sets after the creation came too late (measured:
  // all 20 tasks on CPU 0). Worker i belongs to CPU i % OS_NCPU.
  const unsigned cpu = static_cast<unsigned> (i % OS_NCPU);
  this_thread::thread ().cpu_affinity (1u << cpu);
  while (current_cpu () != cpu)
    {
      this_thread::yield ();
    }
  g_pool.worker_ready ();
#else
  (void)i;
#endif
  g_pool.worker_loop ();
  return nullptr;
}

void
ThreadPool::start ()
{
  for (int i = 0; i < POOL_SIZE; ++i)
    {
      thread::attributes attr = thread::initializer;
      attr.th_stack_address = pool_stacks[i];
      attr.th_stack_size_bytes = sizeof (pool_stacks[i]);

      std::snprintf (pool_thread_names[i], 16, "pool_%02d", i);

      pool_worker_index[i] = i;
      pool_threads[i] = new thread (pool_thread_names[i], pool_worker_func,
                                    &pool_worker_index[i], attr);
#if OS_NCPU > 1
      pool_threads[i]->cpu_affinity (1u << (i % OS_NCPU));
#endif
    }

#if OS_NCPU > 1
  // Wait until every worker has reached its own core before submitting
  // tasks. This is what makes the demo measure the SMP scheduler rather
  // than the speed at which the secondary harts boot: without it, the
  // CPU-0 workers can take all the tasks while the workers pinned to
  // CPUs 1..N-1 are still waiting to be scheduled.
  constexpr unsigned ready_timeout_ms = 2000;
  for (unsigned waited = 0;
       __atomic_load_n (&ready_, __ATOMIC_ACQUIRE) < POOL_SIZE; ++waited)
    {
      if (waited >= ready_timeout_ms)
        {
          write_fmt ("Thread Pool: warning: only %u of %u workers started; "
                     "cores 1..%u have not scheduled their workers\n",
                     ready_, POOL_SIZE, OS_NCPU - 1);
          break;
        }
      sysclock.sleep_for (1);
    }
#endif
}

bool
ThreadPool::submit (task_t func, void* arg)
{
  sem_free_.wait ();

  mutex_.lock ();
  queue_[tail_].func = func;
  queue_[tail_].arg = arg;
  tail_ = (tail_ + 1) % TASK_QUEUE_SIZE;
  count_ = count_ + 1;
  mutex_.unlock ();

  sem_tasks_.post ();
  return true;
}

void
ThreadPool::worker_loop ()
{
  while (true)
    {
      sem_tasks_.wait ();

      mutex_.lock ();
      Task task = queue_[head_];
      head_ = (head_ + 1) % TASK_QUEUE_SIZE;
      count_ = count_ - 1;
      mutex_.unlock ();

      sem_free_.post ();

      if (task.func)
        {
          task.func (task.arg);
        }
    }
}

static volatile int g_pool_task_count = 0;
static volatile unsigned g_pool_tasks_on_cpu[OS_NCPU];
static mutex g_pool_print_mutex;

static void
pool_demo_task (void* arg)
{
  int id = static_cast<int> (reinterpret_cast<std::intptr_t> (arg));
  volatile int sum = 0;
  for (int i = 0; i < 10000; ++i)
    {
      sum = sum + i;
    }
  g_pool_print_mutex.lock ();
  write_fmt ("Thread Pool: Worker executed task %d on CPU %u (mhartid %u)\n",
             id, current_cpu (), current_mhartid ());
  g_pool_task_count = g_pool_task_count + 1;
  g_pool_tasks_on_cpu[current_cpu ()] = g_pool_tasks_on_cpu[current_cpu ()] + 1;
  g_pool_print_mutex.unlock ();
}

// ----------------------------------------------------------------------------
// Solver globals
// ----------------------------------------------------------------------------
static Barrier g_barrier_lu;
static Barrier g_barrier_step;
static volatile bool g_solver_done = false;
static volatile std::uint32_t g_block_start_us = 0;
static volatile std::uint32_t g_block_end_us = 0;

static float A[N][N];
static float b[N];

static float A_block[N][N];
static float b_block[N];
static float x_block[N];

static float x_classic[N];

static float LU_k_T[B][B];
static int pivot_k_T[B];

static thread::stack::element_t solver_stacks[OS_NCPU][1024];
// The hart each solver thread actually ran on, read from mhartid by the
// thread itself: the proof that the work was spread over the cores.
static volatile unsigned solver_mhartid[OS_NCPU];
static thread* solver_threads[OS_NCPU];
static char solver_thread_names[OS_NCPU][16];

// ----------------------------------------------------------------------------
// LU utilities for B x B blocks
// ----------------------------------------------------------------------------
static bool
lu_decompose (const float M[B][B], float LU[B][B], int pivot[B])
{
  for (int i = 0; i < B; ++i)
    {
      for (int j = 0; j < B; ++j)
        {
          LU[i][j] = M[i][j];
        }
      pivot[i] = i;
    }
  for (int i = 0; i < B; ++i)
    {
      float max_val = 0.0f;
      int max_row = i;
      for (int r = i; r < B; ++r)
        {
          float val = std::abs (LU[r][i]);
          if (val > max_val)
            {
              max_val = val;
              max_row = r;
            }
        }
      if (max_val < 1e-9f)
        {
          return false; // Singular
        }
      if (max_row != i)
        {
          std::swap (pivot[i], pivot[max_row]);
          for (int c = 0; c < B; ++c)
            {
              std::swap (LU[i][c], LU[max_row][c]);
            }
        }
      for (int r = i + 1; r < B; ++r)
        {
          float factor = LU[r][i] / LU[i][i];
          LU[r][i] = factor;
          for (int c = i + 1; c < B; ++c)
            {
              LU[r][c] = LU[r][c] - factor * LU[i][c];
            }
        }
    }
  return true;
}

static void
lu_solve (const float LU[B][B], const int pivot[B], const float Y[B],
          float X[B])
{
  float temp[B];
  for (int i = 0; i < B; ++i)
    {
      temp[i] = Y[pivot[i]];
      for (int j = 0; j < i; ++j)
        {
          temp[i] = temp[i] - LU[i][j] * temp[j];
        }
    }
  for (int i = B - 1; i >= 0; --i)
    {
      X[i] = temp[i];
      for (int j = i + 1; j < B; ++j)
        {
          X[i] = X[i] - LU[i][j] * X[j];
        }
      X[i] = X[i] / LU[i][i];
    }
}

// Solve A_T * M_T = Src_T and transpose the result back to Dest.
static void
solve_block_transpose (const float LU_T[B][B], const int pivot_T[B],
                       const float Src[B][B], float Dest[B][B],
                       float Src_T[B][B], float M_T[B][B])
{
  for (int i = 0; i < B; ++i)
    {
      for (int j = 0; j < B; ++j)
        {
          Src_T[i][j] = Src[j][i];
        }
    }
  for (int col = 0; col < B; ++col)
    {
      float y[B];
      float x[B];
      for (int r = 0; r < B; ++r)
        {
          y[r] = Src_T[r][col];
        }
      lu_solve (LU_T, pivot_T, y, x);
      for (int r = 0; r < B; ++r)
        {
          M_T[r][col] = x[r];
        }
    }
  for (int i = 0; i < B; ++i)
    {
      for (int j = 0; j < B; ++j)
        {
          Dest[i][j] = M_T[j][i];
        }
    }
}

static void
mat_mul_sub (const float M[B][B], const float X[B][B], float Dest[B][B])
{
  for (int i = 0; i < B; ++i)
    {
      for (int j = 0; j < B; ++j)
        {
          float sum = 0.0f;
          for (int k = 0; k < B; ++k)
            {
              sum = sum + M[i][k] * X[k][j];
            }
          Dest[i][j] = Dest[i][j] - sum;
        }
    }
}

static void
vec_mul_sub (const float M[B][B], const float v[B], float Dest[B])
{
  for (int i = 0; i < B; ++i)
    {
      float sum = 0.0f;
      for (int k = 0; k < B; ++k)
        {
          sum = sum + M[i][k] * v[k];
        }
      Dest[i] = Dest[i] - sum;
    }
}

static void
get_matrix_block (const float Src[N][N], int br, int bc, float Dest[B][B])
{
  for (int i = 0; i < B; ++i)
    {
      for (int j = 0; j < B; ++j)
        {
          Dest[i][j] = Src[br * B + i][bc * B + j];
        }
    }
}

static void
set_matrix_block (float Dest[N][N], int br, int bc, const float Src[B][B])
{
  for (int i = 0; i < B; ++i)
    {
      for (int j = 0; j < B; ++j)
        {
          Dest[br * B + i][bc * B + j] = Src[i][j];
        }
    }
}

// ----------------------------------------------------------------------------
// Per-core solver worker (block parallel elimination)
// ----------------------------------------------------------------------------
static void*
solver_thread_func (void* arg)
{
  int core_id = static_cast<int> (reinterpret_cast<std::intptr_t> (arg));
  int num_blocks = N / B;

  // Per-core workspace (static to keep thread stacks tiny). A_kj doubles as
  // the A_ik scratch: it is only needed again after solve_block_transpose.
  static float workspace_Src_T[OS_NCPU][B][B];
  static float workspace_M_T[OS_NCPU][B][B];
  static float M_local[OS_NCPU][B][B];
  static float A_kj[OS_NCPU][B][B];
  static float A_ij[OS_NCPU][B][B];

#if OS_NCPU > 1
  // Pin first, then get there: a new thread runs on whichever core picks
  // it up before os_main's cpu_affinity() call, and stays there (measured:
  // solver_c0 on mhartid 4, c1 and c2 both on mhartid 2). As smp-test2.
  this_thread::thread ().cpu_affinity (1u << core_id);
  while (current_cpu () != static_cast<unsigned> (core_id))
    {
      this_thread::yield ();
    }
#endif
  solver_mhartid[core_id] = current_mhartid ();
  if (core_id == 0)
    {
      g_block_start_us = timer_us32 ();
    }

  for (int k = 0; k < num_blocks - 1; ++k)
    {
      if (core_id == 0)
        {
          // Core 0: LU-decompose the diagonal block A_kk (transposed layout).
          static float A_kk_T[B][B];
          for (int i = 0; i < B; ++i)
            {
              for (int j = 0; j < B; ++j)
                {
                  A_kk_T[i][j] = A_block[k * B + j][k * B + i];
                }
            }
          lu_decompose (A_kk_T, LU_k_T, pivot_k_T);
        }

      g_barrier_lu.wait ();

      // All cores: compute multipliers + update their assigned row blocks.
      for (int i = k + 1; i < num_blocks; ++i)
        {
          if ((i - (k + 1)) % OS_NCPU == core_id)
            {
              // A_ik = A_block[i][k]
              get_matrix_block (A_block, i, k, A_kj[core_id]);
              solve_block_transpose (
                  LU_k_T, pivot_k_T, A_kj[core_id], M_local[core_id],
                  workspace_Src_T[core_id], workspace_M_T[core_id]);

              // A_ij = A_ij - M_ik * A_kj  for j > k
              for (int j = k + 1; j < num_blocks; ++j)
                {
                  get_matrix_block (A_block, k, j, A_kj[core_id]);
                  get_matrix_block (A_block, i, j, A_ij[core_id]);
                  mat_mul_sub (M_local[core_id], A_kj[core_id],
                               A_ij[core_id]);
                  set_matrix_block (A_block, i, j, A_ij[core_id]);
                }

              // b_i = b_i - M_ik * b_k
              vec_mul_sub (M_local[core_id], &b_block[k * B],
                           &b_block[i * B]);
            }
        }

      g_barrier_step.wait ();
    }

  // Final block k = num_blocks - 1: sequential back substitution (core 0).
  if (core_id == 0)
    {
      static float A_ii[B][B];
      static float LU_ii[B][B];
      static float A_ij_temp[B][B];
      static int pivot_ii[B];
      static float temp_bi[B];

      for (int i = num_blocks - 1; i >= 0; --i)
        {
          for (int r = 0; r < B; ++r)
            {
              temp_bi[r] = b_block[i * B + r];
            }
          for (int j = i + 1; j < num_blocks; ++j)
            {
              get_matrix_block (A_block, i, j, A_ij_temp);
              for (int r = 0; r < B; ++r)
                {
                  for (int c = 0; c < B; ++c)
                    {
                      temp_bi[r] = temp_bi[r]
                                   - A_ij_temp[r][c] * x_block[j * B + c];
                    }
                }
            }
          get_matrix_block (A_block, i, i, A_ii);
          lu_decompose (A_ii, LU_ii, pivot_ii);
          lu_solve (LU_ii, pivot_ii, temp_bi, &x_block[i * B]);
        }

      g_block_end_us = timer_us32 ();
      g_solver_done = true;
    }

  g_barrier_lu.wait (); // sync all cores at exit

  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return nullptr;
}

// ----------------------------------------------------------------------------
// Classical N x N LU solve (ground truth, core 0)
// ----------------------------------------------------------------------------
static bool
solve_classical (const float src_A[N][N], const float src_b[N],
                 float dest_x[N])
{
  static float LU[N][N];
  static int pivot[N];
  float temp[N];

  for (int i = 0; i < N; ++i)
    {
      for (int j = 0; j < N; ++j)
        {
          LU[i][j] = src_A[i][j];
        }
      pivot[i] = i;
    }

  for (int i = 0; i < N; ++i)
    {
      float max_val = 0.0f;
      int max_row = i;
      for (int r = i; r < N; ++r)
        {
          float val = std::abs (LU[r][i]);
          if (val > max_val)
            {
              max_val = val;
              max_row = r;
            }
        }
      if (max_val < 1e-9f)
        {
          return false; // Singular
        }
      if (max_row != i)
        {
          std::swap (pivot[i], pivot[max_row]);
          for (int c = 0; c < N; ++c)
            {
              std::swap (LU[i][c], LU[max_row][c]);
            }
        }
      for (int r = i + 1; r < N; ++r)
        {
          float factor = LU[r][i] / LU[i][i];
          LU[r][i] = factor;
          for (int c = i + 1; c < N; ++c)
            {
              LU[r][c] = LU[r][c] - factor * LU[i][c];
            }
        }
    }

  // Forward substitution (L * temp = b_pivoted).
  for (int i = 0; i < N; ++i)
    {
      temp[i] = src_b[pivot[i]];
      for (int j = 0; j < i; ++j)
        {
          temp[i] = temp[i] - LU[i][j] * temp[j];
        }
    }

  // Backward substitution (U * x = temp).
  for (int i = N - 1; i >= 0; --i)
    {
      dest_x[i] = temp[i];
      for (int j = i + 1; j < N; ++j)
        {
          dest_x[i] = dest_x[i] - LU[i][j] * dest_x[j];
        }
      dest_x[i] = dest_x[i] / LU[i][i];
    }

  return true;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
#if OS_NCPU > 1
// The idle thread of CPUs 1..3. No wfi on this port: it yields.
static void*
secondary_idle_func (void*)
{
  this_thread::thread ().priority (thread::priority::idle);
  while (true)
    {
      this_thread::yield ();
    }
  return nullptr;
}
#endif

int
os_main (int /*argc*/, char* /*argv*/[])
{
  using namespace os::rtos;
#if OS_NCPU > 1
  // The main thread (it launches the other cores) stays on core 0.
  this_thread::thread ().cpu_affinity (1u << 0);
#endif


  led::init ();
  timer_init ();

  g_barrier_lu.init (OS_NCPU, 2);   // (the slot numbers are the RP2350's)
  g_barrier_step.init (OS_NCPU, 3);
  g_solver_done = false;

  write_str ("\n");
  write_str ("=============================================\n");
  write_str ("  " PORT_BANNER_LONG " - micro-os-plus-iii\n");
  write_str ("  smp-mat-test : parallel block linear solver\n");
  write_fmt ("  N = %d, B = %d, cores = %d\n", N, B, OS_NCPU);
  write_str ("=============================================\n");

#if OS_NCPU > 1
  // CPUs 1..OS_NCPU-1: each gets its idle thread ("idle<n>", registered in
  // os_idle_thread_core[n] before the launch), then is released from the
  // port's park onto its own boot stack.
  {
    static thread::stack::element_t idle_stacks[OS_NCPU][512];
    static char idle_names[OS_NCPU][8];
    for (unsigned c = 1; c < OS_NCPU; ++c)
      {
        thread::attributes attr = thread::initializer;
        attr.th_stack_address = idle_stacks[c];
        attr.th_stack_size_bytes = sizeof (idle_stacks[c]);
        std::snprintf (idle_names[c], sizeof (idle_names[c]), "idle%u", c);
        os::rtos::scheduler::os_idle_thread_core[c]
            = new thread (idle_names[c], secondary_idle_func, nullptr, attr);
      }
    for (unsigned c = 1; c < OS_NCPU; ++c)
      {
        write_fmt ("core 0: launching core %u (mhartid %u)...\n", c,
                   c + RISCV_HART_BASE);
        multicore::launch_core (c, port_smp_secondary_start,
                                port_secondary_stack_top (c));
      }
    port_smp_core0_ipi_enable ();
    write_str ("core 0: all cores launched; SMP running.\n");
  }
#endif

  // ---- Thread Pool demo ----------------------------------------------------
  write_str ("Starting Thread Pool of 10 worker threads...\n");
  g_pool.start ();

  write_str ("Submitting 20 tasks to the Thread Pool...\n");
  for (int i = 0; i < 20; ++i)
    {
      g_pool.submit (pool_demo_task, reinterpret_cast<void*> (i));
    }

  while (g_pool_task_count < 20)
    {
      sysclock.sleep_for (10);
    }
  write_str ("Thread Pool demo finished. All 20 tasks executed successfully.\n");
  // Tasks per CPU: every core must have run some (a real SMP pool).
  bool pool_ok = true;
  write_str ("Thread Pool: tasks per CPU:");
  for (unsigned c = 0; c < OS_NCPU; ++c)
    {
      write_fmt (" CPU %u = %u", c, g_pool_tasks_on_cpu[c]);
      pool_ok = pool_ok && (g_pool_tasks_on_cpu[c] > 0);
    }
  write_str (pool_ok ? "  (every core)\n\n" : "  (NOT every core)\n\n");

  // ---- Validate blocks / generate the diagonally dominant system -----------
  if (N % B != 0)
    {
      write_fmt ("ERROR: N (%d) must be divisible by B (%d)!\n", N, B);
      hw_result::fail (); // a configuration error ends the run (no wfi park)
      for (;;)
        {
        }
    }
  int num_blocks = N / B;
  if (num_blocks % OS_NCPU != 0)
    {
      write_fmt ("ERROR: number of blocks N/B (%d) must be a multiple of %d!\n",
                 num_blocks, OS_NCPU);
      hw_result::fail (); // a configuration error ends the run (no wfi park)
      for (;;)
        {
        }
    }

  write_fmt ("Generating diagonally dominant %d x %d matrix...\n", N, N);
  srand (42);
  for (int i = 0; i < N; ++i)
    {
      for (int j = 0; j < N; ++j)
        {
          A[i][j]
              = -5.0f + static_cast<float> (rand ())
                            / (static_cast<float> (RAND_MAX) / 10.0f);
        }
    }
  for (int i = 0; i < N; ++i)
    {
      float sum = 0.0f;
      for (int j = 0; j < N; ++j)
        {
          if (i != j)
            {
              sum = sum + std::abs (A[i][j]);
            }
        }
      A[i][i] = sum + 5.0f; // strictly diagonally dominant
      b[i] = 1.0f;
    }

  for (int i = 0; i < N; ++i)
    {
      for (int j = 0; j < N; ++j)
        {
          A_block[i][j] = A[i][j];
        }
      b_block[i] = b[i];
    }

  // ---- Launch the parallel solver threads ----------------------------------
  write_fmt ("Starting parallel %d-core block elimination...\n", OS_NCPU);
  for (int c = 0; c < OS_NCPU; ++c)
    {
      thread::attributes attr = thread::initializer;
      attr.th_stack_address = solver_stacks[c];
      attr.th_stack_size_bytes = sizeof (solver_stacks[c]);

      std::snprintf (solver_thread_names[c], 16, "solver_c%d", c);

      solver_threads[c]
          = new thread (solver_thread_names[c], solver_thread_func,
                        reinterpret_cast<void*> (static_cast<std::intptr_t> (c)), attr);
#if OS_NCPU > 1
      solver_threads[c]->cpu_affinity (1u << c);
#endif
    }

  bool led_state = false;
  while (!g_solver_done)
    {
      sysclock.sleep_for (100);
      led_state = !led_state;
      if (led_state)
        {
          led::on ();
        }
      else
        {
          led::off ();
        }
    }
  led::off ();
  write_str ("Block elimination finished.\n");

  // ---- Classical solve (ground truth) --------------------------------------
  write_str ("Solving full system using classical elimination...\n");
  std::uint32_t classic_start = timer_us32 ();
  solve_classical (A, b, x_classic);
  std::uint32_t classic_end = timer_us32 ();
  write_str ("Classical elimination finished.\n\n");

  // ---- Performance + comparison --------------------------------------------
  std::uint32_t block_us = g_block_end_us - g_block_start_us;
  std::uint32_t classic_us = classic_end - classic_start;

  write_str ("+---------------------------------------------------+\n");
  write_str ("| COMPARING BLOCK ELIMINATION VS CLASSICAL SOLVER  |\n");
  write_str ("+---------------------------------------------------+\n");
  float max_diff = 0.0f;
  float sum_diff = 0.0f;
  for (int i = 0; i < N; ++i)
    {
      float diff = std::abs (x_block[i] - x_classic[i]);
      sum_diff = sum_diff + diff;
      if (diff > max_diff)
        {
          max_diff = diff;
        }
      // Every element is compared; every tenth is printed (x[0], x[10], ...)
      // -- each line costs semihosting round trips on the board.
      if ((i % 10) != 0)
        {
          continue;
        }
      write_str ("x[");
      write_fmt ("%d", i);
      write_str ("] Block = ");
      print_float (x_block[i]);
      write_str (" | Classic = ");
      print_float (x_classic[i]);
      write_str (" | Abs Diff = ");
      print_float (diff);
      write_str ("\n");
    }
  write_str ("-----------------------------------------------------\n");
  write_str ("Total Absolute Difference (Sum): ");
  print_float (sum_diff);
  write_str ("\n");
  write_str ("Max Absolute Difference:        ");
  print_float (max_diff);
  write_str ("\n");
  write_str ("-----------------------------------------------------\n");
  write_str ("Performance Metrics:\n");
  write_str ("  Parallel Block Solver: ");
  print_time_us (static_cast<double> (block_us));
  write_str ("\n");
  write_str ("  Classical Solver:      ");
  print_time_us (static_cast<double> (classic_us));
  write_str ("\n");
  write_str ("  Speedup Ratio:         ");
  print_ratio (classic_us > 0
                   ? static_cast<double> (classic_us)
                         / static_cast<double> (block_us)
                   : 0.0);
  write_str ("x\n");
  write_str ("+---------------------------------------------------+\n");

  // Each solver thread c must have run on CPU c (mhartid c + RISCV_HART_BASE).
  bool cores_ok = true;
  for (unsigned c = 0; c < OS_NCPU; ++c)
    {
      write_fmt ("  solver_c%u ran on mhartid %u\n", c, solver_mhartid[c]);
      cores_ok = cores_ok && (solver_mhartid[c] == c + RISCV_HART_BASE);
    }
  bool pass = (max_diff < 0.01f) && cores_ok && pool_ok;
  write_fmt ("RESULT: %s\n",
             pass ? "PASS (block == classical, one solver per core, pool on every core)"
                  : (!cores_ok ? "FAIL (a solver ran on the wrong core)"
                     : (!pool_ok ? "FAIL (the thread pool did not use every core)"
                                 : "FAIL (solutions differ)")));
  write_str ("---------------------------------------------\n");

  // End the run with the verdict (semihosting exit).
  if (pass)
    {
      hw_result::ok ();
    }
  else
    {
      hw_result::fail ();
    }

  for (;;)
    {
      sysclock.sleep_for (200);
      led_toggle ();
    }
  return 0;
}
