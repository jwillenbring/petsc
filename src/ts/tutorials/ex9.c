static const char help[] = "1D periodic Finite Volume solver in slope-limiter form with semidiscrete time stepping.\n"
                           "Solves scalar and vector problems, choose the physical model with -physics\n"
                           "  advection   - Constant coefficient scalar advection\n"
                           "                u_t       + (a*u)_x               = 0\n"
                           "  burgers     - Burgers equation\n"
                           "                u_t       + (u^2/2)_x             = 0\n"
                           "  traffic     - Traffic equation\n"
                           "                u_t       + (u*(1-u))_x           = 0\n"
                           "  acoustics   - Acoustic wave propagation\n"
                           "                u_t       + (c*z*v)_x             = 0\n"
                           "                v_t       + (c/z*u)_x             = 0\n"
                           "  isogas      - Isothermal gas dynamics\n"
                           "                rho_t     + (rho*u)_x             = 0\n"
                           "                (rho*u)_t + (rho*u^2 + c^2*rho)_x = 0\n"
                           "  shallow     - Shallow water equations\n"
                           "                h_t       + (h*u)_x               = 0\n"
                           "                (h*u)_t   + (h*u^2 + g*h^2/2)_x   = 0\n"
                           "Some of these physical models have multiple Riemann solvers, select these with -physics_xxx_riemann\n"
                           "  exact       - Exact Riemann solver which usually needs to perform a Newton iteration to connect\n"
                           "                the states across shocks and rarefactions\n"
                           "  roe         - Linearized scheme, usually with an entropy fix inside sonic rarefactions\n"
                           "The systems provide a choice of reconstructions with -physics_xxx_reconstruct\n"
                           "  characteristic - Limit the characteristic variables, this is usually preferred (default)\n"
                           "  conservative   - Limit the conservative variables directly, can cause undesired interaction of waves\n\n"
                           "A variety of limiters for high-resolution TVD limiters are available with -limit\n"
                           "  upwind,minmod,superbee,mc,vanleer,vanalbada,koren,cada-torillhon (last two are nominally third order)\n"
                           "  and non-TVD schemes lax-wendroff,beam-warming,fromm\n\n"
                           "To preserve the TVD property, one should time step with a strong stability preserving method.\n"
                           "The optimal high order explicit Runge-Kutta methods in TSSSP are recommended for non-stiff problems.\n\n"
                           "Several initial conditions can be chosen with -initial N\n\n"
                           "The problem size should be set with -da_grid_x M\n\n";

#include <petscts.h>
#include <petscdm.h>
#include <petscdmda.h>
#include <petscdraw.h>

#include <petsc/private/kernels/blockinvert.h> /* For the Kernel_*_gets_* stuff for BAIJ */

static inline PetscReal Sgn(PetscReal a)
{
  return (a < 0) ? -1 : 1;
}
static inline PetscReal Abs(PetscReal a)
{
  return (a < 0) ? 0 : a;
}
static inline PetscReal Sqr(PetscReal a)
{
  return a * a;
}
static inline PetscReal MaxAbs(PetscReal a, PetscReal b)
{
  return (PetscAbs(a) > PetscAbs(b)) ? a : b;
}
PETSC_UNUSED static inline PetscReal MinAbs(PetscReal a, PetscReal b)
{
  return (PetscAbs(a) < PetscAbs(b)) ? a : b;
}
static inline PetscReal MinMod2(PetscReal a, PetscReal b)
{
  return (a * b < 0) ? 0 : Sgn(a) * PetscMin(PetscAbs(a), PetscAbs(b));
}
static inline PetscReal MaxMod2(PetscReal a, PetscReal b)
{
  return (a * b < 0) ? 0 : Sgn(a) * PetscMax(PetscAbs(a), PetscAbs(b));
}
static inline PetscReal MinMod3(PetscReal a, PetscReal b, PetscReal c)
{
  return (a * b < 0 || a * c < 0) ? 0 : Sgn(a) * PetscMin(PetscAbs(a), PetscMin(PetscAbs(b), PetscAbs(c)));
}

static inline PetscReal RangeMod(PetscReal a, PetscReal xmin, PetscReal xmax)
{
  PetscReal range = xmax - xmin;
  return xmin + PetscFmodReal(range + PetscFmodReal(a, range), range);
}

/* ----------------------- Lots of limiters, these could go in a separate library ------------------------- */
typedef struct _LimitInfo {
  PetscReal hx;
  PetscInt  m;
}          *LimitInfo;
static void Limit_Upwind(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = 0;
}
static void Limit_LaxWendroff(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = jR[i];
}
static void Limit_BeamWarming(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = jL[i];
}
static void Limit_Fromm(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = 0.5 * (jL[i] + jR[i]);
}
static void Limit_Minmod(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = MinMod2(jL[i], jR[i]);
}
static void Limit_Superbee(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = MaxMod2(MinMod2(jL[i], 2 * jR[i]), MinMod2(2 * jL[i], jR[i]));
}
static void Limit_MC(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = MinMod3(2 * jL[i], 0.5 * (jL[i] + jR[i]), 2 * jR[i]);
}
static void Limit_VanLeer(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{ /* phi = (t + abs(t)) / (1 + abs(t)) */
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = (jL[i] * Abs(jR[i]) + Abs(jL[i]) * jR[i]) / (Abs(jL[i]) + Abs(jR[i]) + 1e-15);
}
static void Limit_VanAlbada(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt) /* differentiable */
{                                                                                                           /* phi = (t + t^2) / (1 + t^2) */
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = (jL[i] * Sqr(jR[i]) + Sqr(jL[i]) * jR[i]) / (Sqr(jL[i]) + Sqr(jR[i]) + 1e-15);
}
static void Limit_VanAlbadaTVD(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{ /* phi = (t + t^2) / (1 + t^2) */
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = (jL[i] * jR[i] < 0) ? 0 : (jL[i] * Sqr(jR[i]) + Sqr(jL[i]) * jR[i]) / (Sqr(jL[i]) + Sqr(jR[i]) + 1e-15);
}
static void Limit_Koren(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt) /* differentiable */
{                                                                                                       /* phi = (t + 2*t^2) / (2 - t + 2*t^2) */
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = ((jL[i] * Sqr(jR[i]) + 2 * Sqr(jL[i]) * jR[i]) / (2 * Sqr(jL[i]) - jL[i] * jR[i] + 2 * Sqr(jR[i]) + 1e-15));
}
static void Limit_KorenSym(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt) /* differentiable */
{                                                                                                          /* Symmetric version of above */
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = (1.5 * (jL[i] * Sqr(jR[i]) + Sqr(jL[i]) * jR[i]) / (2 * Sqr(jL[i]) - jL[i] * jR[i] + 2 * Sqr(jR[i]) + 1e-15));
}
static void Limit_Koren3(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{ /* Eq 11 of Cada-Torrilhon 2009 */
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = MinMod3(2 * jL[i], (jL[i] + 2 * jR[i]) / 3, 2 * jR[i]);
}
static PetscReal CadaTorrilhonPhiHatR_Eq13(PetscReal L, PetscReal R)
{
  return PetscMax(0, PetscMin((L + 2 * R) / 3, PetscMax(-0.5 * L, PetscMin(2 * L, PetscMin((L + 2 * R) / 3, 1.6 * R)))));
}
static void Limit_CadaTorrilhon2(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{ /* Cada-Torrilhon 2009, Eq 13 */
  PetscInt i;
  for (i = 0; i < info->m; i++) lmt[i] = CadaTorrilhonPhiHatR_Eq13(jL[i], jR[i]);
}
static void Limit_CadaTorrilhon3R(PetscReal r, LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{ /* Cada-Torrilhon 2009, Eq 22 */
  /* They recommend 0.001 < r < 1, but larger values are more accurate in smooth regions */
  const PetscReal eps = 1e-7, hx = info->hx;
  PetscInt        i;
  for (i = 0; i < info->m; i++) {
    const PetscReal eta = (Sqr(jL[i]) + Sqr(jR[i])) / Sqr(r * hx);
    lmt[i] = ((eta < 1 - eps) ? (jL[i] + 2 * jR[i]) / 3 : ((eta > 1 + eps) ? CadaTorrilhonPhiHatR_Eq13(jL[i], jR[i]) : 0.5 * ((1 - (eta - 1) / eps) * (jL[i] + 2 * jR[i]) / 3 + (1 + (eta + 1) / eps) * CadaTorrilhonPhiHatR_Eq13(jL[i], jR[i]))));
  }
}
static void Limit_CadaTorrilhon3R0p1(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  Limit_CadaTorrilhon3R(0.1, info, jL, jR, lmt);
}
static void Limit_CadaTorrilhon3R1(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  Limit_CadaTorrilhon3R(1, info, jL, jR, lmt);
}
static void Limit_CadaTorrilhon3R10(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  Limit_CadaTorrilhon3R(10, info, jL, jR, lmt);
}
static void Limit_CadaTorrilhon3R100(LimitInfo info, const PetscScalar *jL, const PetscScalar *jR, PetscScalar *lmt)
{
  Limit_CadaTorrilhon3R(100, info, jL, jR, lmt);
}

/* --------------------------------- Finite Volume data structures ----------------------------------- */

typedef enum {
  FVBC_PERIODIC,
  FVBC_OUTFLOW
} FVBCType;
static const char *FVBCTypes[] = {"PERIODIC", "OUTFLOW", "FVBCType", "FVBC_", 0};
typedef PetscErrorCode (*RiemannFunction)(void *, PetscInt, const PetscScalar *, const PetscScalar *, PetscScalar *, PetscReal *);
typedef PetscErrorCode (*ReconstructFunction)(void *, PetscInt, const PetscScalar *, PetscScalar *, PetscScalar *, PetscReal *);

typedef struct {
  PetscErrorCode (*sample)(void *, PetscInt, FVBCType, PetscReal, PetscReal, PetscReal, PetscReal, PetscReal *);
  RiemannFunction     riemann;
  ReconstructFunction characteristic;
  PetscErrorCode (*destroy)(void *);
  void    *user;
  PetscInt dof;
  char    *fieldname[16];
} PhysicsCtx;

typedef struct {
  void (*limit)(LimitInfo, const PetscScalar *, const PetscScalar *, PetscScalar *);
  PhysicsCtx physics;
  MPI_Comm   comm;
  char       prefix[256];

  /* Local work arrays */
  PetscScalar *R, *Rinv; /* Characteristic basis, and it's inverse.  COLUMN-MAJOR */
  PetscScalar *cjmpLR;   /* Jumps at left and right edge of cell, in characteristic basis, len=2*dof */
  PetscScalar *cslope;   /* Limited slope, written in characteristic basis */
  PetscScalar *uLR;      /* Solution at left and right of interface, conservative variables, len=2*dof */
  PetscScalar *flux;     /* Flux across interface */
  PetscReal   *speeds;   /* Speeds of each wave */

  PetscReal cfl_idt; /* Max allowable value of 1/Delta t */
  PetscReal cfl;
  PetscReal xmin, xmax;
  PetscInt  initial;
  PetscBool exact;
  FVBCType  bctype;
} FVCtx;

PetscErrorCode RiemannListAdd(PetscFunctionList *flist, const char *name, RiemannFunction rsolve)
{
  PetscFunctionBeginUser;
  PetscCall(PetscFunctionListAdd(flist, name, rsolve));
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode RiemannListFind(PetscFunctionList flist, const char *name, RiemannFunction *rsolve)
{
  PetscFunctionBeginUser;
  PetscCall(PetscFunctionListFind(flist, name, rsolve));
  PetscCheck(*rsolve, PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "Riemann solver \"%s\" could not be found", name);
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode ReconstructListAdd(PetscFunctionList *flist, const char *name, ReconstructFunction r)
{
  PetscFunctionBeginUser;
  PetscCall(PetscFunctionListAdd(flist, name, r));
  PetscFunctionReturn(PETSC_SUCCESS);
}

PetscErrorCode ReconstructListFind(PetscFunctionList flist, const char *name, ReconstructFunction *r)
{
  PetscFunctionBeginUser;
  PetscCall(PetscFunctionListFind(flist, name, r));
  PetscCheck(*r, PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "Reconstruction \"%s\" could not be found", name);
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* --------------------------------- Physics ----------------------------------- */
/*
  Each physical model consists of Riemann solver and a function to determine the basis to use for reconstruction.  These
  are set with the PhysicsCreate_XXX function which allocates private storage and sets these methods as well as the
  number of fields and their names, and a function to deallocate private storage.
*/

/* First a few functions useful to several different physics */
static PetscErrorCode PhysicsCharacteristic_Conservative(void *vctx, PetscInt m, const PetscScalar *u, PetscScalar *X, PetscScalar *Xi, PetscReal *speeds)
{
  PetscInt i, j;

  PetscFunctionBeginUser;
  for (i = 0; i < m; i++) {
    for (j = 0; j < m; j++) Xi[i * m + j] = X[i * m + j] = (PetscScalar)(i == j);
    speeds[i] = PETSC_MAX_REAL; /* Indicates invalid */
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsDestroy_SimpleFree(void *vctx)
{
  PetscFunctionBeginUser;
  PetscCall(PetscFree(vctx));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* --------------------------------- Advection ----------------------------------- */

typedef struct {
  PetscReal a; /* advective velocity */
} AdvectCtx;

static PetscErrorCode PhysicsRiemann_Advect(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  AdvectCtx *ctx = (AdvectCtx *)vctx;
  PetscReal  speed;

  PetscFunctionBeginUser;
  speed     = ctx->a;
  flux[0]   = PetscMax(0, speed) * uL[0] + PetscMin(0, speed) * uR[0];
  *maxspeed = speed;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCharacteristic_Advect(void *vctx, PetscInt m, const PetscScalar *u, PetscScalar *X, PetscScalar *Xi, PetscReal *speeds)
{
  AdvectCtx *ctx = (AdvectCtx *)vctx;

  PetscFunctionBeginUser;
  X[0]      = 1.;
  Xi[0]     = 1.;
  speeds[0] = ctx->a;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsSample_Advect(void *vctx, PetscInt initial, FVBCType bctype, PetscReal xmin, PetscReal xmax, PetscReal t, PetscReal x, PetscReal *u)
{
  AdvectCtx *ctx = (AdvectCtx *)vctx;
  PetscReal  a   = ctx->a, x0;

  PetscFunctionBeginUser;
  switch (bctype) {
  case FVBC_OUTFLOW:
    x0 = x - a * t;
    break;
  case FVBC_PERIODIC:
    x0 = RangeMod(x - a * t, xmin, xmax);
    break;
  default:
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "unknown BCType");
  }
  switch (initial) {
  case 0:
    u[0] = (x0 < 0) ? 1 : -1;
    break;
  case 1:
    u[0] = (x0 < 0) ? -1 : 1;
    break;
  case 2:
    u[0] = (0 < x0 && x0 < 1) ? 1 : 0;
    break;
  case 3:
    u[0] = PetscSinReal(2 * PETSC_PI * x0);
    break;
  case 4:
    u[0] = PetscAbs(x0);
    break;
  case 5:
    u[0] = (x0 < 0 || x0 > 0.5) ? 0 : PetscSqr(PetscSinReal(2 * PETSC_PI * x0));
    break;
  case 6:
    u[0] = (x0 < 0) ? 0 : ((x0 < 1) ? x0 : ((x0 < 2) ? 2 - x0 : 0));
    break;
  case 7:
    u[0] = PetscPowReal(PetscSinReal(PETSC_PI * x0), 10.0);
    break;
  default:
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "unknown initial condition");
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCreate_Advect(FVCtx *ctx)
{
  AdvectCtx *user;

  PetscFunctionBeginUser;
  PetscCall(PetscNew(&user));
  ctx->physics.sample         = PhysicsSample_Advect;
  ctx->physics.riemann        = PhysicsRiemann_Advect;
  ctx->physics.characteristic = PhysicsCharacteristic_Advect;
  ctx->physics.destroy        = PhysicsDestroy_SimpleFree;
  ctx->physics.user           = user;
  ctx->physics.dof            = 1;
  PetscCall(PetscStrallocpy("u", &ctx->physics.fieldname[0]));
  user->a = 1;
  PetscOptionsBegin(ctx->comm, ctx->prefix, "Options for advection", "");
  {
    PetscCall(PetscOptionsReal("-physics_advect_a", "Speed", "", user->a, &user->a, NULL));
  }
  PetscOptionsEnd();
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* --------------------------------- Burgers ----------------------------------- */

typedef struct {
  PetscReal lxf_speed;
} BurgersCtx;

static PetscErrorCode PhysicsSample_Burgers(void *vctx, PetscInt initial, FVBCType bctype, PetscReal xmin, PetscReal xmax, PetscReal t, PetscReal x, PetscReal *u)
{
  PetscFunctionBeginUser;
  PetscCheck(bctype != FVBC_PERIODIC || t <= 0, PETSC_COMM_SELF, PETSC_ERR_SUP, "Exact solution not implemented for periodic");
  switch (initial) {
  case 0:
    u[0] = (x < 0) ? 1 : -1;
    break;
  case 1:
    if (x < -t) u[0] = -1;
    else if (x < t) u[0] = x / t;
    else u[0] = 1;
    break;
  case 2:
    if (x <= 0) u[0] = 0;
    else if (x < t) u[0] = x / t;
    else if (x < 1 + 0.5 * t) u[0] = 1;
    else u[0] = 0;
    break;
  case 3:
    if (x < 0.2 * t) u[0] = 0.2;
    else if (x < t) u[0] = x / t;
    else u[0] = 1;
    break;
  case 4:
    PetscCheck(t <= 0, PETSC_COMM_SELF, PETSC_ERR_SUP, "Only initial condition available");
    u[0] = 0.7 + 0.3 * PetscSinReal(2 * PETSC_PI * ((x - xmin) / (xmax - xmin)));
    break;
  case 5: /* Pure shock solution */
    if (x < 0.5 * t) u[0] = 1;
    else u[0] = 0;
    break;
  default:
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "unknown initial condition");
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Burgers_Exact(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  PetscFunctionBeginUser;
  if (uL[0] < uR[0]) {                /* rarefaction */
    flux[0] = (uL[0] * uR[0] < 0) ? 0 /* sonic rarefaction */
                                  : 0.5 * PetscMin(PetscSqr(uL[0]), PetscSqr(uR[0]));
  } else { /* shock */
    flux[0] = 0.5 * PetscMax(PetscSqr(uL[0]), PetscSqr(uR[0]));
  }
  *maxspeed = (PetscAbs(uL[0]) > PetscAbs(uR[0])) ? uL[0] : uR[0];
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Burgers_Roe(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  PetscReal speed;

  PetscFunctionBeginUser;
  speed   = 0.5 * (uL[0] + uR[0]);
  flux[0] = 0.25 * (PetscSqr(uL[0]) + PetscSqr(uR[0])) - 0.5 * PetscAbs(speed) * (uR[0] - uL[0]);
  if (uL[0] <= 0 && 0 <= uR[0]) flux[0] = 0; /* Entropy fix for sonic rarefaction */
  *maxspeed = speed;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Burgers_LxF(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  PetscReal   c;
  PetscScalar fL, fR;

  PetscFunctionBeginUser;
  c         = ((BurgersCtx *)vctx)->lxf_speed;
  fL        = 0.5 * PetscSqr(uL[0]);
  fR        = 0.5 * PetscSqr(uR[0]);
  flux[0]   = 0.5 * (fL + fR) - 0.5 * c * (uR[0] - uL[0]);
  *maxspeed = c;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Burgers_Rusanov(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  PetscReal   c;
  PetscScalar fL, fR;

  PetscFunctionBeginUser;
  c         = PetscMax(PetscAbs(uL[0]), PetscAbs(uR[0]));
  fL        = 0.5 * PetscSqr(uL[0]);
  fR        = 0.5 * PetscSqr(uR[0]);
  flux[0]   = 0.5 * (fL + fR) - 0.5 * c * (uR[0] - uL[0]);
  *maxspeed = c;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCreate_Burgers(FVCtx *ctx)
{
  BurgersCtx       *user;
  RiemannFunction   r;
  PetscFunctionList rlist      = 0;
  char              rname[256] = "exact";

  PetscFunctionBeginUser;
  PetscCall(PetscNew(&user));

  ctx->physics.sample         = PhysicsSample_Burgers;
  ctx->physics.characteristic = PhysicsCharacteristic_Conservative;
  ctx->physics.destroy        = PhysicsDestroy_SimpleFree;
  ctx->physics.user           = user;
  ctx->physics.dof            = 1;

  PetscCall(PetscStrallocpy("u", &ctx->physics.fieldname[0]));
  PetscCall(RiemannListAdd(&rlist, "exact", PhysicsRiemann_Burgers_Exact));
  PetscCall(RiemannListAdd(&rlist, "roe", PhysicsRiemann_Burgers_Roe));
  PetscCall(RiemannListAdd(&rlist, "lxf", PhysicsRiemann_Burgers_LxF));
  PetscCall(RiemannListAdd(&rlist, "rusanov", PhysicsRiemann_Burgers_Rusanov));
  PetscOptionsBegin(ctx->comm, ctx->prefix, "Options for advection", "");
  {
    PetscCall(PetscOptionsFList("-physics_burgers_riemann", "Riemann solver", "", rlist, rname, rname, sizeof(rname), NULL));
  }
  PetscOptionsEnd();
  PetscCall(RiemannListFind(rlist, rname, &r));
  PetscCall(PetscFunctionListDestroy(&rlist));
  ctx->physics.riemann = r;

  /* *
  * Hack to deal with LxF in semi-discrete form
  * max speed is 1 for the basic initial conditions (where |u| <= 1)
  * */
  if (r == PhysicsRiemann_Burgers_LxF) user->lxf_speed = 1;
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* --------------------------------- Traffic ----------------------------------- */

typedef struct {
  PetscReal lxf_speed;
  PetscReal a;
} TrafficCtx;

static inline PetscScalar TrafficFlux(PetscScalar a, PetscScalar u)
{
  return a * u * (1 - u);
}

static PetscErrorCode PhysicsSample_Traffic(void *vctx, PetscInt initial, FVBCType bctype, PetscReal xmin, PetscReal xmax, PetscReal t, PetscReal x, PetscReal *u)
{
  PetscReal a = ((TrafficCtx *)vctx)->a;

  PetscFunctionBeginUser;
  PetscCheck(bctype != FVBC_PERIODIC || t <= 0, PETSC_COMM_SELF, PETSC_ERR_SUP, "Exact solution not implemented for periodic");
  switch (initial) {
  case 0:
    u[0] = (-a * t < x) ? 2 : 0;
    break;
  case 1:
    if (x < PetscMin(2 * a * t, 0.5 + a * t)) u[0] = -1;
    else if (x < 1) u[0] = 0;
    else u[0] = 1;
    break;
  case 2:
    PetscCheck(t <= 0, PETSC_COMM_SELF, PETSC_ERR_SUP, "Only initial condition available");
    u[0] = 0.7 + 0.3 * PetscSinReal(2 * PETSC_PI * ((x - xmin) / (xmax - xmin)));
    break;
  default:
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "unknown initial condition");
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Traffic_Exact(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  PetscReal a = ((TrafficCtx *)vctx)->a;

  PetscFunctionBeginUser;
  if (uL[0] < uR[0]) {
    flux[0] = PetscMin(TrafficFlux(a, uL[0]), TrafficFlux(a, uR[0]));
  } else {
    flux[0] = (uR[0] < 0.5 && 0.5 < uL[0]) ? TrafficFlux(a, 0.5) : PetscMax(TrafficFlux(a, uL[0]), TrafficFlux(a, uR[0]));
  }
  *maxspeed = a * MaxAbs(1 - 2 * uL[0], 1 - 2 * uR[0]);
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Traffic_Roe(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  PetscReal a = ((TrafficCtx *)vctx)->a;
  PetscReal speed;

  PetscFunctionBeginUser;
  speed     = a * (1 - (uL[0] + uR[0]));
  flux[0]   = 0.5 * (TrafficFlux(a, uL[0]) + TrafficFlux(a, uR[0])) - 0.5 * PetscAbs(speed) * (uR[0] - uL[0]);
  *maxspeed = speed;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Traffic_LxF(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  TrafficCtx *phys = (TrafficCtx *)vctx;
  PetscReal   a    = phys->a;
  PetscReal   speed;

  PetscFunctionBeginUser;
  speed     = a * (1 - (uL[0] + uR[0]));
  flux[0]   = 0.5 * (TrafficFlux(a, uL[0]) + TrafficFlux(a, uR[0])) - 0.5 * phys->lxf_speed * (uR[0] - uL[0]);
  *maxspeed = speed;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Traffic_Rusanov(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  PetscReal a = ((TrafficCtx *)vctx)->a;
  PetscReal speed;

  PetscFunctionBeginUser;
  speed     = a * PetscMax(PetscAbs(1 - 2 * uL[0]), PetscAbs(1 - 2 * uR[0]));
  flux[0]   = 0.5 * (TrafficFlux(a, uL[0]) + TrafficFlux(a, uR[0])) - 0.5 * speed * (uR[0] - uL[0]);
  *maxspeed = speed;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCreate_Traffic(FVCtx *ctx)
{
  TrafficCtx       *user;
  RiemannFunction   r;
  PetscFunctionList rlist      = 0;
  char              rname[256] = "exact";

  PetscFunctionBeginUser;
  PetscCall(PetscNew(&user));
  ctx->physics.sample         = PhysicsSample_Traffic;
  ctx->physics.characteristic = PhysicsCharacteristic_Conservative;
  ctx->physics.destroy        = PhysicsDestroy_SimpleFree;
  ctx->physics.user           = user;
  ctx->physics.dof            = 1;

  PetscCall(PetscStrallocpy("density", &ctx->physics.fieldname[0]));
  user->a = 0.5;
  PetscCall(RiemannListAdd(&rlist, "exact", PhysicsRiemann_Traffic_Exact));
  PetscCall(RiemannListAdd(&rlist, "roe", PhysicsRiemann_Traffic_Roe));
  PetscCall(RiemannListAdd(&rlist, "lxf", PhysicsRiemann_Traffic_LxF));
  PetscCall(RiemannListAdd(&rlist, "rusanov", PhysicsRiemann_Traffic_Rusanov));
  PetscOptionsBegin(ctx->comm, ctx->prefix, "Options for Traffic", "");
  PetscCall(PetscOptionsReal("-physics_traffic_a", "Flux = a*u*(1-u)", "", user->a, &user->a, NULL));
  PetscCall(PetscOptionsFList("-physics_traffic_riemann", "Riemann solver", "", rlist, rname, rname, sizeof(rname), NULL));
  PetscOptionsEnd();

  PetscCall(RiemannListFind(rlist, rname, &r));
  PetscCall(PetscFunctionListDestroy(&rlist));

  ctx->physics.riemann = r;

  /* *
  * Hack to deal with LxF in semi-discrete form
  * max speed is 3*a for the basic initial conditions (-1 <= u <= 2)
  * */
  if (r == PhysicsRiemann_Traffic_LxF) user->lxf_speed = 3 * user->a;
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* --------------------------------- Linear Acoustics ----------------------------------- */

/* Flux: u_t + (A u)_x
 * z = sqrt(rho*bulk), c = sqrt(rho/bulk)
 * Spectral decomposition: A = R * D * Rinv
 * [    cz] = [-z   z] [-c    ] [-1/2z  1/2]
 * [c/z   ] = [ 1   1] [     c] [ 1/2z  1/2]
 *
 * We decompose this into the left-traveling waves Al = R * D^- Rinv
 * and the right-traveling waves Ar = R * D^+ * Rinv
 * Multiplying out these expressions produces the following two matrices
 */

typedef struct {
  PetscReal c; /* speed of sound: c = sqrt(bulk/rho) */
  PetscReal z; /* impedance: z = sqrt(rho*bulk) */
} AcousticsCtx;

PETSC_UNUSED static inline void AcousticsFlux(AcousticsCtx *ctx, const PetscScalar *u, PetscScalar *f)
{
  f[0] = ctx->c * ctx->z * u[1];
  f[1] = ctx->c / ctx->z * u[0];
}

static PetscErrorCode PhysicsCharacteristic_Acoustics(void *vctx, PetscInt m, const PetscScalar *u, PetscScalar *X, PetscScalar *Xi, PetscReal *speeds)
{
  AcousticsCtx *phys = (AcousticsCtx *)vctx;
  PetscReal     z = phys->z, c = phys->c;

  PetscFunctionBeginUser;
  X[0 * 2 + 0]  = -z;
  X[0 * 2 + 1]  = z;
  X[1 * 2 + 0]  = 1;
  X[1 * 2 + 1]  = 1;
  Xi[0 * 2 + 0] = -1. / (2 * z);
  Xi[0 * 2 + 1] = 1. / 2;
  Xi[1 * 2 + 0] = 1. / (2 * z);
  Xi[1 * 2 + 1] = 1. / 2;
  speeds[0]     = -c;
  speeds[1]     = c;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsSample_Acoustics_Initial(AcousticsCtx *phys, PetscInt initial, PetscReal xmin, PetscReal xmax, PetscReal x, PetscReal *u)
{
  PetscFunctionBeginUser;
  switch (initial) {
  case 0:
    u[0] = (PetscAbs((x - xmin) / (xmax - xmin) - 0.2) < 0.1) ? 1 : 0.5;
    u[1] = (PetscAbs((x - xmin) / (xmax - xmin) - 0.7) < 0.1) ? 1 : -0.5;
    break;
  case 1:
    u[0] = PetscCosReal(3 * 2 * PETSC_PI * x / (xmax - xmin));
    u[1] = PetscExpReal(-PetscSqr(x - (xmax + xmin) / 2) / (2 * PetscSqr(0.2 * (xmax - xmin)))) - 0.5;
    break;
  default:
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "unknown initial condition");
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsSample_Acoustics(void *vctx, PetscInt initial, FVBCType bctype, PetscReal xmin, PetscReal xmax, PetscReal t, PetscReal x, PetscReal *u)
{
  AcousticsCtx *phys = (AcousticsCtx *)vctx;
  PetscReal     c    = phys->c;
  PetscReal     x0a, x0b, u0a[2], u0b[2], tmp[2];
  PetscReal     X[2][2], Xi[2][2], dummy[2];

  PetscFunctionBeginUser;
  switch (bctype) {
  case FVBC_OUTFLOW:
    x0a = x + c * t;
    x0b = x - c * t;
    break;
  case FVBC_PERIODIC:
    x0a = RangeMod(x + c * t, xmin, xmax);
    x0b = RangeMod(x - c * t, xmin, xmax);
    break;
  default:
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "unknown BCType");
  }
  PetscCall(PhysicsSample_Acoustics_Initial(phys, initial, xmin, xmax, x0a, u0a));
  PetscCall(PhysicsSample_Acoustics_Initial(phys, initial, xmin, xmax, x0b, u0b));
  PetscCall(PhysicsCharacteristic_Acoustics(vctx, 2, u, &X[0][0], &Xi[0][0], dummy));
  tmp[0] = Xi[0][0] * u0a[0] + Xi[0][1] * u0a[1];
  tmp[1] = Xi[1][0] * u0b[0] + Xi[1][1] * u0b[1];
  u[0]   = X[0][0] * tmp[0] + X[0][1] * tmp[1];
  u[1]   = X[1][0] * tmp[0] + X[1][1] * tmp[1];
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Acoustics_Exact(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  AcousticsCtx *phys = (AcousticsCtx *)vctx;
  PetscReal     c = phys->c, z = phys->z;
  PetscReal     Al[2][2] =
    {
      {-c / 2,      c * z / 2},
      {c / (2 * z), -c / 2   }
  },         /* Left traveling waves */
    Ar[2][2] = {{c / 2, c * z / 2}, {c / (2 * z), c / 2}}; /* Right traveling waves */

  PetscFunctionBeginUser;
  flux[0]   = Al[0][0] * uR[0] + Al[0][1] * uR[1] + Ar[0][0] * uL[0] + Ar[0][1] * uL[1];
  flux[1]   = Al[1][0] * uR[0] + Al[1][1] * uR[1] + Ar[1][0] * uL[0] + Ar[1][1] * uL[1];
  *maxspeed = c;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCreate_Acoustics(FVCtx *ctx)
{
  AcousticsCtx     *user;
  PetscFunctionList rlist = 0, rclist = 0;
  char              rname[256] = "exact", rcname[256] = "characteristic";

  PetscFunctionBeginUser;
  PetscCall(PetscNew(&user));
  ctx->physics.sample  = PhysicsSample_Acoustics;
  ctx->physics.destroy = PhysicsDestroy_SimpleFree;
  ctx->physics.user    = user;
  ctx->physics.dof     = 2;

  PetscCall(PetscStrallocpy("u", &ctx->physics.fieldname[0]));
  PetscCall(PetscStrallocpy("v", &ctx->physics.fieldname[1]));

  user->c = 1;
  user->z = 1;

  PetscCall(RiemannListAdd(&rlist, "exact", PhysicsRiemann_Acoustics_Exact));
  PetscCall(ReconstructListAdd(&rclist, "characteristic", PhysicsCharacteristic_Acoustics));
  PetscCall(ReconstructListAdd(&rclist, "conservative", PhysicsCharacteristic_Conservative));
  PetscOptionsBegin(ctx->comm, ctx->prefix, "Options for linear Acoustics", "");
  {
    PetscCall(PetscOptionsReal("-physics_acoustics_c", "c = sqrt(bulk/rho)", "", user->c, &user->c, NULL));
    PetscCall(PetscOptionsReal("-physics_acoustics_z", "z = sqrt(bulk*rho)", "", user->z, &user->z, NULL));
    PetscCall(PetscOptionsFList("-physics_acoustics_riemann", "Riemann solver", "", rlist, rname, rname, sizeof(rname), NULL));
    PetscCall(PetscOptionsFList("-physics_acoustics_reconstruct", "Reconstruction", "", rclist, rcname, rcname, sizeof(rcname), NULL));
  }
  PetscOptionsEnd();
  PetscCall(RiemannListFind(rlist, rname, &ctx->physics.riemann));
  PetscCall(ReconstructListFind(rclist, rcname, &ctx->physics.characteristic));
  PetscCall(PetscFunctionListDestroy(&rlist));
  PetscCall(PetscFunctionListDestroy(&rclist));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* --------------------------------- Isothermal Gas Dynamics ----------------------------------- */

typedef struct {
  PetscReal acoustic_speed;
} IsoGasCtx;

static inline void IsoGasFlux(PetscReal c, const PetscScalar *u, PetscScalar *f)
{
  f[0] = u[1];
  f[1] = PetscSqr(u[1]) / u[0] + c * c * u[0];
}

static PetscErrorCode PhysicsSample_IsoGas(void *vctx, PetscInt initial, FVBCType bctype, PetscReal xmin, PetscReal xmax, PetscReal t, PetscReal x, PetscReal *u)
{
  PetscFunctionBeginUser;
  PetscCheck(t <= 0, PETSC_COMM_SELF, PETSC_ERR_SUP, "Exact solutions not implemented for t > 0");
  switch (initial) {
  case 0:
    u[0] = (x < 0) ? 1 : 0.5;
    u[1] = (x < 0) ? 1 : 0.7;
    break;
  case 1:
    u[0] = 1 + 0.5 * PetscSinReal(2 * PETSC_PI * x);
    u[1] = 1 * u[0];
    break;
  default:
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "unknown initial condition");
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_IsoGas_Roe(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  IsoGasCtx  *phys = (IsoGasCtx *)vctx;
  PetscReal   c    = phys->acoustic_speed;
  PetscScalar ubar, du[2], a[2], fL[2], fR[2], lam[2], ustar[2], R[2][2];
  PetscInt    i;

  PetscFunctionBeginUser;
  ubar = (uL[1] / PetscSqrtScalar(uL[0]) + uR[1] / PetscSqrtScalar(uR[0])) / (PetscSqrtScalar(uL[0]) + PetscSqrtScalar(uR[0]));
  /* write fluxuations in characteristic basis */
  du[0] = uR[0] - uL[0];
  du[1] = uR[1] - uL[1];
  a[0]  = (1 / (2 * c)) * ((ubar + c) * du[0] - du[1]);
  a[1]  = (1 / (2 * c)) * ((-ubar + c) * du[0] + du[1]);
  /* wave speeds */
  lam[0] = ubar - c;
  lam[1] = ubar + c;
  /* Right eigenvectors */
  R[0][0] = 1;
  R[0][1] = ubar - c;
  R[1][0] = 1;
  R[1][1] = ubar + c;
  /* Compute state in star region (between the 1-wave and 2-wave) */
  for (i = 0; i < 2; i++) ustar[i] = uL[i] + a[0] * R[0][i];
  if (uL[1] / uL[0] < c && c < ustar[1] / ustar[0]) { /* 1-wave is sonic rarefaction */
    PetscScalar ufan[2];
    ufan[0] = uL[0] * PetscExpScalar(uL[1] / (uL[0] * c) - 1);
    ufan[1] = c * ufan[0];
    IsoGasFlux(c, ufan, flux);
  } else if (ustar[1] / ustar[0] < -c && -c < uR[1] / uR[0]) { /* 2-wave is sonic rarefaction */
    PetscScalar ufan[2];
    ufan[0] = uR[0] * PetscExpScalar(-uR[1] / (uR[0] * c) - 1);
    ufan[1] = -c * ufan[0];
    IsoGasFlux(c, ufan, flux);
  } else { /* Centered form */
    IsoGasFlux(c, uL, fL);
    IsoGasFlux(c, uR, fR);
    for (i = 0; i < 2; i++) {
      PetscScalar absdu = PetscAbsScalar(lam[0]) * a[0] * R[0][i] + PetscAbsScalar(lam[1]) * a[1] * R[1][i];
      flux[i]           = 0.5 * (fL[i] + fR[i]) - 0.5 * absdu;
    }
  }
  *maxspeed = MaxAbs(lam[0], lam[1]);
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_IsoGas_Exact(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  IsoGasCtx  *phys = (IsoGasCtx *)vctx;
  PetscReal   c    = phys->acoustic_speed;
  PetscScalar ustar[2];
  struct {
    PetscScalar rho, u;
  } L = {uL[0], uL[1] / uL[0]}, R = {uR[0], uR[1] / uR[0]}, star;
  PetscInt i;

  PetscFunctionBeginUser;
  PetscCheck((L.rho > 0 && R.rho > 0), PETSC_COMM_SELF, PETSC_ERR_ARG_OUTOFRANGE, "Reconstructed density is negative");
  {
    /* Solve for star state */
    PetscScalar res, tmp, rho = 0.5 * (L.rho + R.rho); /* initial guess */
    for (i = 0; i < 20; i++) {
      PetscScalar fr, fl, dfr, dfl;
      fl  = (L.rho < rho) ? (rho - L.rho) / PetscSqrtScalar(L.rho * rho) /* shock */
                          : PetscLogScalar(rho) - PetscLogScalar(L.rho); /* rarefaction */
      fr  = (R.rho < rho) ? (rho - R.rho) / PetscSqrtScalar(R.rho * rho) /* shock */
                          : PetscLogScalar(rho) - PetscLogScalar(R.rho); /* rarefaction */
      res = R.u - L.u + c * (fr + fl);
      PetscCheck(!PetscIsInfOrNanScalar(res), PETSC_COMM_SELF, PETSC_ERR_FP, "Infinity or Not-a-Number generated in computation");
      if (PetscAbsScalar(res) < 1e-10) {
        star.rho = rho;
        star.u   = L.u - c * fl;
        goto converged;
      }
      dfl = (L.rho < rho) ? 1 / PetscSqrtScalar(L.rho * rho) * (1 - 0.5 * (rho - L.rho) / rho) : 1 / rho;
      dfr = (R.rho < rho) ? 1 / PetscSqrtScalar(R.rho * rho) * (1 - 0.5 * (rho - R.rho) / rho) : 1 / rho;
      tmp = rho - res / (c * (dfr + dfl));
      if (tmp <= 0) rho /= 2; /* Guard against Newton shooting off to a negative density */
      else rho = tmp;
      PetscCheck(((rho > 0) && PetscIsNormalScalar(rho)), PETSC_COMM_SELF, PETSC_ERR_FP, "non-normal iterate rho=%g", (double)PetscRealPart(rho));
    }
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_CONV_FAILED, "Newton iteration for star.rho diverged after %" PetscInt_FMT " iterations", i);
  }
converged:
  if (L.u - c < 0 && 0 < star.u - c) { /* 1-wave is sonic rarefaction */
    PetscScalar ufan[2];
    ufan[0] = L.rho * PetscExpScalar(L.u / c - 1);
    ufan[1] = c * ufan[0];
    IsoGasFlux(c, ufan, flux);
  } else if (star.u + c < 0 && 0 < R.u + c) { /* 2-wave is sonic rarefaction */
    PetscScalar ufan[2];
    ufan[0] = R.rho * PetscExpScalar(-R.u / c - 1);
    ufan[1] = -c * ufan[0];
    IsoGasFlux(c, ufan, flux);
  } else if ((L.rho >= star.rho && L.u - c >= 0) || (L.rho < star.rho && (star.rho * star.u - L.rho * L.u) / (star.rho - L.rho) > 0)) {
    /* 1-wave is supersonic rarefaction, or supersonic shock */
    IsoGasFlux(c, uL, flux);
  } else if ((star.rho <= R.rho && R.u + c <= 0) || (star.rho > R.rho && (R.rho * R.u - star.rho * star.u) / (R.rho - star.rho) < 0)) {
    /* 2-wave is supersonic rarefaction or supersonic shock */
    IsoGasFlux(c, uR, flux);
  } else {
    ustar[0] = star.rho;
    ustar[1] = star.rho * star.u;
    IsoGasFlux(c, ustar, flux);
  }
  *maxspeed = MaxAbs(MaxAbs(star.u - c, star.u + c), MaxAbs(L.u - c, R.u + c));
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_IsoGas_Rusanov(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  IsoGasCtx  *phys = (IsoGasCtx *)vctx;
  PetscScalar c    = phys->acoustic_speed, fL[2], fR[2], s;
  struct {
    PetscScalar rho, u;
  } L = {uL[0], uL[1] / uL[0]}, R = {uR[0], uR[1] / uR[0]};

  PetscFunctionBeginUser;
  PetscCheck((L.rho > 0 && R.rho > 0), PETSC_COMM_SELF, PETSC_ERR_ARG_OUTOFRANGE, "Reconstructed density is negative");
  IsoGasFlux(c, uL, fL);
  IsoGasFlux(c, uR, fR);
  s         = PetscMax(PetscAbs(L.u), PetscAbs(R.u)) + c;
  flux[0]   = 0.5 * (fL[0] + fR[0]) + 0.5 * s * (uL[0] - uR[0]);
  flux[1]   = 0.5 * (fL[1] + fR[1]) + 0.5 * s * (uL[1] - uR[1]);
  *maxspeed = s;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCharacteristic_IsoGas(void *vctx, PetscInt m, const PetscScalar *u, PetscScalar *X, PetscScalar *Xi, PetscReal *speeds)
{
  IsoGasCtx *phys = (IsoGasCtx *)vctx;
  PetscReal  c    = phys->acoustic_speed;

  PetscFunctionBeginUser;
  speeds[0]    = u[1] / u[0] - c;
  speeds[1]    = u[1] / u[0] + c;
  X[0 * 2 + 0] = 1;
  X[0 * 2 + 1] = speeds[0];
  X[1 * 2 + 0] = 1;
  X[1 * 2 + 1] = speeds[1];
  PetscCall(PetscArraycpy(Xi, X, 4));
  PetscCall(PetscKernel_A_gets_inverse_A_2(Xi, 0, PETSC_FALSE, NULL));
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCreate_IsoGas(FVCtx *ctx)
{
  IsoGasCtx        *user;
  PetscFunctionList rlist = 0, rclist = 0;
  char              rname[256] = "exact", rcname[256] = "characteristic";

  PetscFunctionBeginUser;
  PetscCall(PetscNew(&user));
  ctx->physics.sample  = PhysicsSample_IsoGas;
  ctx->physics.destroy = PhysicsDestroy_SimpleFree;
  ctx->physics.user    = user;
  ctx->physics.dof     = 2;

  PetscCall(PetscStrallocpy("density", &ctx->physics.fieldname[0]));
  PetscCall(PetscStrallocpy("momentum", &ctx->physics.fieldname[1]));

  user->acoustic_speed = 1;

  PetscCall(RiemannListAdd(&rlist, "exact", PhysicsRiemann_IsoGas_Exact));
  PetscCall(RiemannListAdd(&rlist, "roe", PhysicsRiemann_IsoGas_Roe));
  PetscCall(RiemannListAdd(&rlist, "rusanov", PhysicsRiemann_IsoGas_Rusanov));
  PetscCall(ReconstructListAdd(&rclist, "characteristic", PhysicsCharacteristic_IsoGas));
  PetscCall(ReconstructListAdd(&rclist, "conservative", PhysicsCharacteristic_Conservative));
  PetscOptionsBegin(ctx->comm, ctx->prefix, "Options for IsoGas", "");
  PetscCall(PetscOptionsReal("-physics_isogas_acoustic_speed", "Acoustic speed", "", user->acoustic_speed, &user->acoustic_speed, NULL));
  PetscCall(PetscOptionsFList("-physics_isogas_riemann", "Riemann solver", "", rlist, rname, rname, sizeof(rname), NULL));
  PetscCall(PetscOptionsFList("-physics_isogas_reconstruct", "Reconstruction", "", rclist, rcname, rcname, sizeof(rcname), NULL));
  PetscOptionsEnd();
  PetscCall(RiemannListFind(rlist, rname, &ctx->physics.riemann));
  PetscCall(ReconstructListFind(rclist, rcname, &ctx->physics.characteristic));
  PetscCall(PetscFunctionListDestroy(&rlist));
  PetscCall(PetscFunctionListDestroy(&rclist));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* --------------------------------- Shallow Water ----------------------------------- */
typedef struct {
  PetscReal gravity;
} ShallowCtx;

static inline void ShallowFlux(ShallowCtx *phys, const PetscScalar *u, PetscScalar *f)
{
  f[0] = u[1];
  f[1] = PetscSqr(u[1]) / u[0] + 0.5 * phys->gravity * PetscSqr(u[0]);
}

static PetscErrorCode PhysicsRiemann_Shallow_Exact(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  ShallowCtx *phys = (ShallowCtx *)vctx;
  PetscScalar g    = phys->gravity, ustar[2], cL, cR, c, cstar;
  struct {
    PetscScalar h, u;
  } L = {uL[0], uL[1] / uL[0]}, R = {uR[0], uR[1] / uR[0]}, star;
  PetscInt i;

  PetscFunctionBeginUser;
  PetscCheck((L.h > 0 && R.h > 0), PETSC_COMM_SELF, PETSC_ERR_ARG_OUTOFRANGE, "Reconstructed thickness is negative");
  cL = PetscSqrtScalar(g * L.h);
  cR = PetscSqrtScalar(g * R.h);
  c  = PetscMax(cL, cR);
  {
    /* Solve for star state */
    const PetscInt maxits = 50;
    PetscScalar    tmp, res, res0 = 0, h0, h = 0.5 * (L.h + R.h); /* initial guess */
    h0 = h;
    for (i = 0; i < maxits; i++) {
      PetscScalar fr, fl, dfr, dfl;
      fl  = (L.h < h) ? PetscSqrtScalar(0.5 * g * (h * h - L.h * L.h) * (1 / L.h - 1 / h)) /* shock */
                      : 2 * PetscSqrtScalar(g * h) - 2 * PetscSqrtScalar(g * L.h);         /* rarefaction */
      fr  = (R.h < h) ? PetscSqrtScalar(0.5 * g * (h * h - R.h * R.h) * (1 / R.h - 1 / h)) /* shock */
                      : 2 * PetscSqrtScalar(g * h) - 2 * PetscSqrtScalar(g * R.h);         /* rarefaction */
      res = R.u - L.u + fr + fl;
      PetscCheck(!PetscIsInfOrNanScalar(res), PETSC_COMM_SELF, PETSC_ERR_FP, "Infinity or Not-a-Number generated in computation");
      if (PetscAbsScalar(res) < 1e-8 || (i > 0 && PetscAbsScalar(h - h0) < 1e-8)) {
        star.h = h;
        star.u = L.u - fl;
        goto converged;
      } else if (i > 0 && PetscAbsScalar(res) >= PetscAbsScalar(res0)) { /* Line search */
        h = 0.8 * h0 + 0.2 * h;
        continue;
      }
      /* Accept the last step and take another */
      res0 = res;
      h0   = h;
      dfl  = (L.h < h) ? 0.5 / fl * 0.5 * g * (-L.h * L.h / (h * h) - 1 + 2 * h / L.h) : PetscSqrtScalar(g / h);
      dfr  = (R.h < h) ? 0.5 / fr * 0.5 * g * (-R.h * R.h / (h * h) - 1 + 2 * h / R.h) : PetscSqrtScalar(g / h);
      tmp  = h - res / (dfr + dfl);
      if (tmp <= 0) h /= 2; /* Guard against Newton shooting off to a negative thickness */
      else h = tmp;
      PetscCheck(((h > 0) && PetscIsNormalScalar(h)), PETSC_COMM_SELF, PETSC_ERR_FP, "non-normal iterate h=%g", (double)h);
    }
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_CONV_FAILED, "Newton iteration for star.h diverged after %" PetscInt_FMT " iterations", i);
  }
converged:
  cstar = PetscSqrtScalar(g * star.h);
  if (L.u - cL < 0 && 0 < star.u - cstar) { /* 1-wave is sonic rarefaction */
    PetscScalar ufan[2];
    ufan[0] = 1 / g * PetscSqr(L.u / 3 + 2. / 3 * cL);
    ufan[1] = PetscSqrtScalar(g * ufan[0]) * ufan[0];
    ShallowFlux(phys, ufan, flux);
  } else if (star.u + cstar < 0 && 0 < R.u + cR) { /* 2-wave is sonic rarefaction */
    PetscScalar ufan[2];
    ufan[0] = 1 / g * PetscSqr(R.u / 3 - 2. / 3 * cR);
    ufan[1] = -PetscSqrtScalar(g * ufan[0]) * ufan[0];
    ShallowFlux(phys, ufan, flux);
  } else if ((L.h >= star.h && L.u - c >= 0) || (L.h < star.h && (star.h * star.u - L.h * L.u) / (star.h - L.h) > 0)) {
    /* 1-wave is right-travelling shock (supersonic) */
    ShallowFlux(phys, uL, flux);
  } else if ((star.h <= R.h && R.u + c <= 0) || (star.h > R.h && (R.h * R.u - star.h * star.h) / (R.h - star.h) < 0)) {
    /* 2-wave is left-travelling shock (supersonic) */
    ShallowFlux(phys, uR, flux);
  } else {
    ustar[0] = star.h;
    ustar[1] = star.h * star.u;
    ShallowFlux(phys, ustar, flux);
  }
  *maxspeed = MaxAbs(MaxAbs(star.u - cstar, star.u + cstar), MaxAbs(L.u - cL, R.u + cR));
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsRiemann_Shallow_Rusanov(void *vctx, PetscInt m, const PetscScalar *uL, const PetscScalar *uR, PetscScalar *flux, PetscReal *maxspeed)
{
  ShallowCtx *phys = (ShallowCtx *)vctx;
  PetscScalar g    = phys->gravity, fL[2], fR[2], s;
  struct {
    PetscScalar h, u;
  } L = {uL[0], uL[1] / uL[0]}, R = {uR[0], uR[1] / uR[0]};

  PetscFunctionBeginUser;
  PetscCheck((L.h > 0 && R.h > 0), PETSC_COMM_SELF, PETSC_ERR_ARG_OUTOFRANGE, "Reconstructed thickness is negative");
  ShallowFlux(phys, uL, fL);
  ShallowFlux(phys, uR, fR);
  s         = PetscMax(PetscAbs(L.u) + PetscSqrtScalar(g * L.h), PetscAbs(R.u) + PetscSqrtScalar(g * R.h));
  flux[0]   = 0.5 * (fL[0] + fR[0]) + 0.5 * s * (uL[0] - uR[0]);
  flux[1]   = 0.5 * (fL[1] + fR[1]) + 0.5 * s * (uL[1] - uR[1]);
  *maxspeed = s;
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCharacteristic_Shallow(void *vctx, PetscInt m, const PetscScalar *u, PetscScalar *X, PetscScalar *Xi, PetscReal *speeds)
{
  ShallowCtx *phys = (ShallowCtx *)vctx;
  PetscReal   c;

  PetscFunctionBeginUser;
  c            = PetscSqrtScalar(u[0] * phys->gravity);
  speeds[0]    = u[1] / u[0] - c;
  speeds[1]    = u[1] / u[0] + c;
  X[0 * 2 + 0] = 1;
  X[0 * 2 + 1] = speeds[0];
  X[1 * 2 + 0] = 1;
  X[1 * 2 + 1] = speeds[1];
  PetscCall(PetscArraycpy(Xi, X, 4));
  PetscCall(PetscKernel_A_gets_inverse_A_2(Xi, 0, PETSC_FALSE, NULL));
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode PhysicsCreate_Shallow(FVCtx *ctx)
{
  ShallowCtx       *user;
  PetscFunctionList rlist = 0, rclist = 0;
  char              rname[256] = "exact", rcname[256] = "characteristic";

  PetscFunctionBeginUser;
  PetscCall(PetscNew(&user));
  /* Shallow water and Isothermal Gas dynamics are similar so we reuse initial conditions for now */
  ctx->physics.sample  = PhysicsSample_IsoGas;
  ctx->physics.destroy = PhysicsDestroy_SimpleFree;
  ctx->physics.user    = user;
  ctx->physics.dof     = 2;

  PetscCall(PetscStrallocpy("density", &ctx->physics.fieldname[0]));
  PetscCall(PetscStrallocpy("momentum", &ctx->physics.fieldname[1]));

  user->gravity = 1;

  PetscCall(RiemannListAdd(&rlist, "exact", PhysicsRiemann_Shallow_Exact));
  PetscCall(RiemannListAdd(&rlist, "rusanov", PhysicsRiemann_Shallow_Rusanov));
  PetscCall(ReconstructListAdd(&rclist, "characteristic", PhysicsCharacteristic_Shallow));
  PetscCall(ReconstructListAdd(&rclist, "conservative", PhysicsCharacteristic_Conservative));
  PetscOptionsBegin(ctx->comm, ctx->prefix, "Options for Shallow", "");
  PetscCall(PetscOptionsReal("-physics_shallow_gravity", "Gravity", "", user->gravity, &user->gravity, NULL));
  PetscCall(PetscOptionsFList("-physics_shallow_riemann", "Riemann solver", "", rlist, rname, rname, sizeof(rname), NULL));
  PetscCall(PetscOptionsFList("-physics_shallow_reconstruct", "Reconstruction", "", rclist, rcname, rcname, sizeof(rcname), NULL));
  PetscOptionsEnd();
  PetscCall(RiemannListFind(rlist, rname, &ctx->physics.riemann));
  PetscCall(ReconstructListFind(rclist, rcname, &ctx->physics.characteristic));
  PetscCall(PetscFunctionListDestroy(&rlist));
  PetscCall(PetscFunctionListDestroy(&rclist));
  PetscFunctionReturn(PETSC_SUCCESS);
}

/* --------------------------------- Finite Volume Solver ----------------------------------- */

static PetscErrorCode FVRHSFunction(TS ts, PetscReal time, Vec X, Vec F, void *vctx)
{
  FVCtx       *ctx = (FVCtx *)vctx;
  PetscInt     i, j, k, Mx, dof, xs, xm;
  PetscReal    hx, cfl_idt = 0;
  PetscScalar *x, *f, *slope;
  Vec          Xloc;
  DM           da;

  PetscFunctionBeginUser;
  PetscCall(TSGetDM(ts, &da));
  PetscCall(DMGetLocalVector(da, &Xloc));
  PetscCall(DMDAGetInfo(da, 0, &Mx, 0, 0, 0, 0, 0, &dof, 0, 0, 0, 0, 0));
  hx = (ctx->xmax - ctx->xmin) / Mx;
  PetscCall(DMGlobalToLocalBegin(da, X, INSERT_VALUES, Xloc));
  PetscCall(DMGlobalToLocalEnd(da, X, INSERT_VALUES, Xloc));

  PetscCall(VecZeroEntries(F));

  PetscCall(DMDAVecGetArray(da, Xloc, &x));
  PetscCall(DMDAVecGetArray(da, F, &f));
  PetscCall(DMDAGetArray(da, PETSC_TRUE, &slope));

  PetscCall(DMDAGetCorners(da, &xs, 0, 0, &xm, 0, 0));

  if (ctx->bctype == FVBC_OUTFLOW) {
    for (i = xs - 2; i < 0; i++) {
      for (j = 0; j < dof; j++) x[i * dof + j] = x[j];
    }
    for (i = Mx; i < xs + xm + 2; i++) {
      for (j = 0; j < dof; j++) x[i * dof + j] = x[(xs + xm - 1) * dof + j];
    }
  }
  for (i = xs - 1; i < xs + xm + 1; i++) {
    struct _LimitInfo info;
    PetscScalar      *cjmpL, *cjmpR;
    /* Determine the right eigenvectors R, where A = R \Lambda R^{-1} */
    PetscCall((*ctx->physics.characteristic)(ctx->physics.user, dof, &x[i * dof], ctx->R, ctx->Rinv, ctx->speeds));
    /* Evaluate jumps across interfaces (i-1, i) and (i, i+1), put in characteristic basis */
    PetscCall(PetscArrayzero(ctx->cjmpLR, 2 * dof));
    cjmpL = &ctx->cjmpLR[0];
    cjmpR = &ctx->cjmpLR[dof];
    for (j = 0; j < dof; j++) {
      PetscScalar jmpL, jmpR;
      jmpL = x[(i + 0) * dof + j] - x[(i - 1) * dof + j];
      jmpR = x[(i + 1) * dof + j] - x[(i + 0) * dof + j];
      for (k = 0; k < dof; k++) {
        cjmpL[k] += ctx->Rinv[k + j * dof] * jmpL;
        cjmpR[k] += ctx->Rinv[k + j * dof] * jmpR;
      }
    }
    /* Apply limiter to the left and right characteristic jumps */
    info.m  = dof;
    info.hx = hx;
    (*ctx->limit)(&info, cjmpL, cjmpR, ctx->cslope);
    for (j = 0; j < dof; j++) ctx->cslope[j] /= hx; /* rescale to a slope */
    for (j = 0; j < dof; j++) {
      PetscScalar tmp = 0;
      for (k = 0; k < dof; k++) tmp += ctx->R[j + k * dof] * ctx->cslope[k];
      slope[i * dof + j] = tmp;
    }
  }

  for (i = xs; i < xs + xm + 1; i++) {
    PetscReal    maxspeed;
    PetscScalar *uL, *uR;
    uL = &ctx->uLR[0];
    uR = &ctx->uLR[dof];
    for (j = 0; j < dof; j++) {
      uL[j] = x[(i - 1) * dof + j] + slope[(i - 1) * dof + j] * hx / 2;
      uR[j] = x[(i - 0) * dof + j] - slope[(i - 0) * dof + j] * hx / 2;
    }
    PetscCall((*ctx->physics.riemann)(ctx->physics.user, dof, uL, uR, ctx->flux, &maxspeed));
    cfl_idt = PetscMax(cfl_idt, PetscAbsScalar(maxspeed / hx)); /* Max allowable value of 1/Delta t */

    if (i > xs) {
      for (j = 0; j < dof; j++) f[(i - 1) * dof + j] -= ctx->flux[j] / hx;
    }
    if (i < xs + xm) {
      for (j = 0; j < dof; j++) f[i * dof + j] += ctx->flux[j] / hx;
    }
  }

  PetscCall(DMDAVecRestoreArray(da, Xloc, &x));
  PetscCall(DMDAVecRestoreArray(da, F, &f));
  PetscCall(DMDARestoreArray(da, PETSC_TRUE, &slope));
  PetscCall(DMRestoreLocalVector(da, &Xloc));

  PetscCallMPI(MPI_Allreduce(&cfl_idt, &ctx->cfl_idt, 1, MPIU_REAL, MPIU_MAX, PetscObjectComm((PetscObject)da)));
  if (0) {
    /* We need to a way to inform the TS of a CFL constraint, this is a debugging fragment */
    PetscReal dt, tnow;
    PetscCall(TSGetTimeStep(ts, &dt));
    PetscCall(TSGetTime(ts, &tnow));
    if (dt > 0.5 / ctx->cfl_idt) PetscCall(PetscPrintf(ctx->comm, "Stability constraint exceeded at t=%g, dt %g > %g\n", (double)tnow, (double)dt, (double)(0.5 / ctx->cfl_idt)));
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode SmallMatMultADB(PetscScalar *C, PetscInt bs, const PetscScalar *A, const PetscReal *D, const PetscScalar *B)
{
  PetscInt i, j, k;

  PetscFunctionBeginUser;
  for (i = 0; i < bs; i++) {
    for (j = 0; j < bs; j++) {
      PetscScalar tmp = 0;
      for (k = 0; k < bs; k++) tmp += A[i * bs + k] * D[k] * B[k * bs + j];
      C[i * bs + j] = tmp;
    }
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode FVIJacobian(TS ts, PetscReal t, Vec X, Vec Xdot, PetscReal shift, Mat A, Mat B, void *vctx)
{
  FVCtx             *ctx = (FVCtx *)vctx;
  PetscInt           i, j, dof = ctx->physics.dof;
  PetscScalar       *J;
  const PetscScalar *x;
  PetscReal          hx;
  DM                 da;
  DMDALocalInfo      dainfo;

  PetscFunctionBeginUser;
  PetscCall(TSGetDM(ts, &da));
  PetscCall(DMDAVecGetArrayRead(da, X, (void *)&x));
  PetscCall(DMDAGetLocalInfo(da, &dainfo));
  hx = (ctx->xmax - ctx->xmin) / dainfo.mx;
  PetscCall(PetscMalloc1(dof * dof, &J));
  for (i = dainfo.xs; i < dainfo.xs + dainfo.xm; i++) {
    PetscCall((*ctx->physics.characteristic)(ctx->physics.user, dof, &x[i * dof], ctx->R, ctx->Rinv, ctx->speeds));
    for (j = 0; j < dof; j++) ctx->speeds[j] = PetscAbs(ctx->speeds[j]);
    PetscCall(SmallMatMultADB(J, dof, ctx->R, ctx->speeds, ctx->Rinv));
    for (j = 0; j < dof * dof; j++) J[j] = J[j] / hx + shift * (j / dof == j % dof);
    PetscCall(MatSetValuesBlocked(B, 1, &i, 1, &i, J, INSERT_VALUES));
  }
  PetscCall(PetscFree(J));
  PetscCall(DMDAVecRestoreArrayRead(da, X, (void *)&x));

  PetscCall(MatAssemblyBegin(B, MAT_FINAL_ASSEMBLY));
  PetscCall(MatAssemblyEnd(B, MAT_FINAL_ASSEMBLY));
  if (A != B) {
    PetscCall(MatAssemblyBegin(A, MAT_FINAL_ASSEMBLY));
    PetscCall(MatAssemblyEnd(A, MAT_FINAL_ASSEMBLY));
  }
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode FVSample(FVCtx *ctx, DM da, PetscReal time, Vec U)
{
  PetscScalar *u, *uj;
  PetscInt     i, j, k, dof, xs, xm, Mx;

  PetscFunctionBeginUser;
  PetscCheck(ctx->physics.sample, PETSC_COMM_SELF, PETSC_ERR_SUP, "Physics has not provided a sampling function");
  PetscCall(DMDAGetInfo(da, 0, &Mx, 0, 0, 0, 0, 0, &dof, 0, 0, 0, 0, 0));
  PetscCall(DMDAGetCorners(da, &xs, 0, 0, &xm, 0, 0));
  PetscCall(DMDAVecGetArray(da, U, &u));
  PetscCall(PetscMalloc1(dof, &uj));
  for (i = xs; i < xs + xm; i++) {
    const PetscReal h = (ctx->xmax - ctx->xmin) / Mx, xi = ctx->xmin + h / 2 + i * h;
    const PetscInt  N = 200;
    /* Integrate over cell i using trapezoid rule with N points. */
    for (k = 0; k < dof; k++) u[i * dof + k] = 0;
    for (j = 0; j < N + 1; j++) {
      PetscScalar xj = xi + h * (j - N / 2) / (PetscReal)N;
      PetscCall((*ctx->physics.sample)(ctx->physics.user, ctx->initial, ctx->bctype, ctx->xmin, ctx->xmax, time, xj, uj));
      for (k = 0; k < dof; k++) u[i * dof + k] += ((j == 0 || j == N) ? 0.5 : 1.0) * uj[k] / N;
    }
  }
  PetscCall(DMDAVecRestoreArray(da, U, &u));
  PetscCall(PetscFree(uj));
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode SolutionStatsView(DM da, Vec X, PetscViewer viewer)
{
  PetscReal          xmin, xmax;
  PetscScalar        sum, tvsum, tvgsum;
  const PetscScalar *x;
  PetscInt           imin, imax, Mx, i, j, xs, xm, dof;
  Vec                Xloc;
  PetscBool          iascii;

  PetscFunctionBeginUser;
  PetscCall(PetscObjectTypeCompare((PetscObject)viewer, PETSCVIEWERASCII, &iascii));
  if (iascii) {
    /* PETSc lacks a function to compute total variation norm (difficult in multiple dimensions), we do it here */
    PetscCall(DMGetLocalVector(da, &Xloc));
    PetscCall(DMGlobalToLocalBegin(da, X, INSERT_VALUES, Xloc));
    PetscCall(DMGlobalToLocalEnd(da, X, INSERT_VALUES, Xloc));
    PetscCall(DMDAVecGetArrayRead(da, Xloc, (void *)&x));
    PetscCall(DMDAGetCorners(da, &xs, 0, 0, &xm, 0, 0));
    PetscCall(DMDAGetInfo(da, 0, &Mx, 0, 0, 0, 0, 0, &dof, 0, 0, 0, 0, 0));
    tvsum = 0;
    for (i = xs; i < xs + xm; i++) {
      for (j = 0; j < dof; j++) tvsum += PetscAbsScalar(x[i * dof + j] - x[(i - 1) * dof + j]);
    }
    PetscCallMPI(MPI_Allreduce(&tvsum, &tvgsum, 1, MPIU_REAL, MPIU_SUM, PetscObjectComm((PetscObject)da)));
    PetscCall(DMDAVecRestoreArrayRead(da, Xloc, (void *)&x));
    PetscCall(DMRestoreLocalVector(da, &Xloc));

    PetscCall(VecMin(X, &imin, &xmin));
    PetscCall(VecMax(X, &imax, &xmax));
    PetscCall(VecSum(X, &sum));
    PetscCall(PetscViewerASCIIPrintf(viewer, "Solution range [%8.5f,%8.5f] with extrema at %" PetscInt_FMT " and %" PetscInt_FMT ", mean %8.5f, ||x||_TV %8.5f\n", (double)xmin, (double)xmax, imin, imax, (double)(sum / Mx), (double)(tvgsum / Mx)));
  } else SETERRQ(PETSC_COMM_SELF, PETSC_ERR_SUP, "Viewer type not supported");
  PetscFunctionReturn(PETSC_SUCCESS);
}

static PetscErrorCode SolutionErrorNorms(FVCtx *ctx, DM da, PetscReal t, Vec X, PetscReal *nrm1, PetscReal *nrmsup)
{
  Vec      Y;
  PetscInt Mx;

  PetscFunctionBeginUser;
  PetscCall(VecGetSize(X, &Mx));
  PetscCall(VecDuplicate(X, &Y));
  PetscCall(FVSample(ctx, da, t, Y));
  PetscCall(VecAYPX(Y, -1, X));
  PetscCall(VecNorm(Y, NORM_1, nrm1));
  PetscCall(VecNorm(Y, NORM_INFINITY, nrmsup));
  *nrm1 /= Mx;
  PetscCall(VecDestroy(&Y));
  PetscFunctionReturn(PETSC_SUCCESS);
}

int main(int argc, char *argv[])
{
  char              lname[256] = "mc", physname[256] = "advect", final_fname[256] = "solution.m";
  PetscFunctionList limiters = 0, physics = 0;
  MPI_Comm          comm;
  TS                ts;
  DM                da;
  Vec               X, X0, R;
  Mat               B;
  FVCtx             ctx;
  PetscInt          i, dof, xs, xm, Mx, draw = 0;
  PetscBool         view_final = PETSC_FALSE;
  PetscReal         ptime;

  PetscFunctionBeginUser;
  PetscCall(PetscInitialize(&argc, &argv, 0, help));
  comm = PETSC_COMM_WORLD;
  PetscCall(PetscMemzero(&ctx, sizeof(ctx)));

  /* Register limiters to be available on the command line */
  PetscCall(PetscFunctionListAdd(&limiters, "upwind", Limit_Upwind));
  PetscCall(PetscFunctionListAdd(&limiters, "lax-wendroff", Limit_LaxWendroff));
  PetscCall(PetscFunctionListAdd(&limiters, "beam-warming", Limit_BeamWarming));
  PetscCall(PetscFunctionListAdd(&limiters, "fromm", Limit_Fromm));
  PetscCall(PetscFunctionListAdd(&limiters, "minmod", Limit_Minmod));
  PetscCall(PetscFunctionListAdd(&limiters, "superbee", Limit_Superbee));
  PetscCall(PetscFunctionListAdd(&limiters, "mc", Limit_MC));
  PetscCall(PetscFunctionListAdd(&limiters, "vanleer", Limit_VanLeer));
  PetscCall(PetscFunctionListAdd(&limiters, "vanalbada", Limit_VanAlbada));
  PetscCall(PetscFunctionListAdd(&limiters, "vanalbadatvd", Limit_VanAlbadaTVD));
  PetscCall(PetscFunctionListAdd(&limiters, "koren", Limit_Koren));
  PetscCall(PetscFunctionListAdd(&limiters, "korensym", Limit_KorenSym));
  PetscCall(PetscFunctionListAdd(&limiters, "koren3", Limit_Koren3));
  PetscCall(PetscFunctionListAdd(&limiters, "cada-torrilhon2", Limit_CadaTorrilhon2));
  PetscCall(PetscFunctionListAdd(&limiters, "cada-torrilhon3-r0p1", Limit_CadaTorrilhon3R0p1));
  PetscCall(PetscFunctionListAdd(&limiters, "cada-torrilhon3-r1", Limit_CadaTorrilhon3R1));
  PetscCall(PetscFunctionListAdd(&limiters, "cada-torrilhon3-r10", Limit_CadaTorrilhon3R10));
  PetscCall(PetscFunctionListAdd(&limiters, "cada-torrilhon3-r100", Limit_CadaTorrilhon3R100));

  /* Register physical models to be available on the command line */
  PetscCall(PetscFunctionListAdd(&physics, "advect", PhysicsCreate_Advect));
  PetscCall(PetscFunctionListAdd(&physics, "burgers", PhysicsCreate_Burgers));
  PetscCall(PetscFunctionListAdd(&physics, "traffic", PhysicsCreate_Traffic));
  PetscCall(PetscFunctionListAdd(&physics, "acoustics", PhysicsCreate_Acoustics));
  PetscCall(PetscFunctionListAdd(&physics, "isogas", PhysicsCreate_IsoGas));
  PetscCall(PetscFunctionListAdd(&physics, "shallow", PhysicsCreate_Shallow));

  ctx.comm   = comm;
  ctx.cfl    = 0.9;
  ctx.bctype = FVBC_PERIODIC;
  ctx.xmin   = -1;
  ctx.xmax   = 1;
  PetscOptionsBegin(comm, NULL, "Finite Volume solver options", "");
  PetscCall(PetscOptionsReal("-xmin", "X min", "", ctx.xmin, &ctx.xmin, NULL));
  PetscCall(PetscOptionsReal("-xmax", "X max", "", ctx.xmax, &ctx.xmax, NULL));
  PetscCall(PetscOptionsFList("-limit", "Name of flux limiter to use", "", limiters, lname, lname, sizeof(lname), NULL));
  PetscCall(PetscOptionsFList("-physics", "Name of physics (Riemann solver and characteristics) to use", "", physics, physname, physname, sizeof(physname), NULL));
  PetscCall(PetscOptionsInt("-draw", "Draw solution vector, bitwise OR of (1=initial,2=final,4=final error)", "", draw, &draw, NULL));
  PetscCall(PetscOptionsString("-view_final", "Write final solution in ASCII MATLAB format to given file name", "", final_fname, final_fname, sizeof(final_fname), &view_final));
  PetscCall(PetscOptionsInt("-initial", "Initial condition (depends on the physics)", "", ctx.initial, &ctx.initial, NULL));
  PetscCall(PetscOptionsBool("-exact", "Compare errors with exact solution", "", ctx.exact, &ctx.exact, NULL));
  PetscCall(PetscOptionsReal("-cfl", "CFL number to time step at", "", ctx.cfl, &ctx.cfl, NULL));
  PetscCall(PetscOptionsEnum("-bc_type", "Boundary condition", "", FVBCTypes, (PetscEnum)ctx.bctype, (PetscEnum *)&ctx.bctype, NULL));
  PetscOptionsEnd();

  /* Choose the limiter from the list of registered limiters */
  PetscCall(PetscFunctionListFind(limiters, lname, &ctx.limit));
  PetscCheck(ctx.limit, PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "Limiter '%s' not found", lname);

  /* Choose the physics from the list of registered models */
  {
    PetscErrorCode (*r)(FVCtx *);
    PetscCall(PetscFunctionListFind(physics, physname, &r));
    PetscCheck(r, PETSC_COMM_SELF, PETSC_ERR_ARG_UNKNOWN_TYPE, "Physics '%s' not found", physname);
    /* Create the physics, will set the number of fields and their names */
    PetscCall((*r)(&ctx));
  }

  /* Create a DMDA to manage the parallel grid */
  PetscCall(DMDACreate1d(comm, DM_BOUNDARY_PERIODIC, 50, ctx.physics.dof, 2, NULL, &da));
  PetscCall(DMSetFromOptions(da));
  PetscCall(DMSetUp(da));
  /* Inform the DMDA of the field names provided by the physics. */
  /* The names will be shown in the title bars when run with -ts_monitor_draw_solution */
  for (i = 0; i < ctx.physics.dof; i++) PetscCall(DMDASetFieldName(da, i, ctx.physics.fieldname[i]));
  PetscCall(DMDAGetInfo(da, 0, &Mx, 0, 0, 0, 0, 0, &dof, 0, 0, 0, 0, 0));
  PetscCall(DMDAGetCorners(da, &xs, 0, 0, &xm, 0, 0));

  /* Set coordinates of cell centers */
  PetscCall(DMDASetUniformCoordinates(da, ctx.xmin + 0.5 * (ctx.xmax - ctx.xmin) / Mx, ctx.xmax + 0.5 * (ctx.xmax - ctx.xmin) / Mx, 0, 0, 0, 0));

  /* Allocate work space for the Finite Volume solver (so it doesn't have to be reallocated on each function evaluation) */
  PetscCall(PetscMalloc4(dof * dof, &ctx.R, dof * dof, &ctx.Rinv, 2 * dof, &ctx.cjmpLR, 1 * dof, &ctx.cslope));
  PetscCall(PetscMalloc3(2 * dof, &ctx.uLR, dof, &ctx.flux, dof, &ctx.speeds));

  /* Create a vector to store the solution and to save the initial state */
  PetscCall(DMCreateGlobalVector(da, &X));
  PetscCall(VecDuplicate(X, &X0));
  PetscCall(VecDuplicate(X, &R));

  PetscCall(DMCreateMatrix(da, &B));

  /* Create a time-stepping object */
  PetscCall(TSCreate(comm, &ts));
  PetscCall(TSSetDM(ts, da));
  PetscCall(TSSetRHSFunction(ts, R, FVRHSFunction, &ctx));
  PetscCall(TSSetIJacobian(ts, B, B, FVIJacobian, &ctx));
  PetscCall(TSSetType(ts, TSSSP));
  PetscCall(TSSetMaxTime(ts, 10));
  PetscCall(TSSetExactFinalTime(ts, TS_EXACTFINALTIME_STEPOVER));

  /* Compute initial conditions and starting time step */
  PetscCall(FVSample(&ctx, da, 0, X0));
  PetscCall(FVRHSFunction(ts, 0, X0, X, (void *)&ctx)); /* Initial function evaluation, only used to determine max speed */
  PetscCall(VecCopy(X0, X));                            /* The function value was not used so we set X=X0 again */
  PetscCall(TSSetTimeStep(ts, ctx.cfl / ctx.cfl_idt));
  PetscCall(TSSetFromOptions(ts)); /* Take runtime options */
  PetscCall(SolutionStatsView(da, X, PETSC_VIEWER_STDOUT_WORLD));
  {
    PetscReal nrm1, nrmsup;
    PetscInt  steps;

    PetscCall(TSSolve(ts, X));
    PetscCall(TSGetSolveTime(ts, &ptime));
    PetscCall(TSGetStepNumber(ts, &steps));

    PetscCall(PetscPrintf(comm, "Final time %8.5f, steps %" PetscInt_FMT "\n", (double)ptime, steps));
    if (ctx.exact) {
      PetscCall(SolutionErrorNorms(&ctx, da, ptime, X, &nrm1, &nrmsup));
      PetscCall(PetscPrintf(comm, "Error ||x-x_e||_1 %8.4e  ||x-x_e||_sup %8.4e\n", (double)nrm1, (double)nrmsup));
    }
  }

  PetscCall(SolutionStatsView(da, X, PETSC_VIEWER_STDOUT_WORLD));
  if (draw & 0x1) PetscCall(VecView(X0, PETSC_VIEWER_DRAW_WORLD));
  if (draw & 0x2) PetscCall(VecView(X, PETSC_VIEWER_DRAW_WORLD));
  if (draw & 0x4) {
    Vec Y;
    PetscCall(VecDuplicate(X, &Y));
    PetscCall(FVSample(&ctx, da, ptime, Y));
    PetscCall(VecAYPX(Y, -1, X));
    PetscCall(VecView(Y, PETSC_VIEWER_DRAW_WORLD));
    PetscCall(VecDestroy(&Y));
  }

  if (view_final) {
    PetscViewer viewer;
    PetscCall(PetscViewerASCIIOpen(PETSC_COMM_WORLD, final_fname, &viewer));
    PetscCall(PetscViewerPushFormat(viewer, PETSC_VIEWER_ASCII_MATLAB));
    PetscCall(VecView(X, viewer));
    PetscCall(PetscViewerPopFormat(viewer));
    PetscCall(PetscViewerDestroy(&viewer));
  }

  /* Clean up */
  PetscCall((*ctx.physics.destroy)(ctx.physics.user));
  for (i = 0; i < ctx.physics.dof; i++) PetscCall(PetscFree(ctx.physics.fieldname[i]));
  PetscCall(PetscFree4(ctx.R, ctx.Rinv, ctx.cjmpLR, ctx.cslope));
  PetscCall(PetscFree3(ctx.uLR, ctx.flux, ctx.speeds));
  PetscCall(VecDestroy(&X));
  PetscCall(VecDestroy(&X0));
  PetscCall(VecDestroy(&R));
  PetscCall(MatDestroy(&B));
  PetscCall(DMDestroy(&da));
  PetscCall(TSDestroy(&ts));
  PetscCall(PetscFunctionListDestroy(&limiters));
  PetscCall(PetscFunctionListDestroy(&physics));
  PetscCall(PetscFinalize());
  return 0;
}

/*TEST

    build:
      requires: !complex

    test:
      args: -da_grid_x 100 -initial 1 -xmin -2 -xmax 5 -exact -limit mc
      requires: !complex !single

    test:
      suffix: 2
      args: -da_grid_x 100 -initial 2 -xmin -2 -xmax 2 -exact -limit mc -physics burgers -bc_type outflow -ts_max_time 1
      filter:  sed "s/at 48/at 0/g"
      requires: !complex !single

    test:
      suffix: 3
      args: -da_grid_x 100 -initial 2 -xmin -2 -xmax 2 -exact -limit mc -physics burgers -bc_type outflow -ts_max_time 1
      nsize: 3
      filter:  sed "s/at 48/at 0/g"
      requires: !complex !single

TEST*/
