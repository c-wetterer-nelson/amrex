
#include <AMReX_FArrayBox.H>
#include <AMReX_IArrayBox.H>
#include <AMReX_Geometry.H>
#include <AMReX_Interpolater.H>
#include <AMReX_Interp_C.H>
#include <AMReX_MFInterp_C.H>

#ifndef BL_NO_FORT
#include <AMReX_INTERP_F.H>
#endif

#include <climits>

namespace amrex {

/*
 * PCInterp, NodeBilinear, FaceLinear, CellConservativeLinear, and
 * CellBilinear are supported for all dimensions on cpu and gpu.
 *
 * CellConservativeProtected only works in 2D and 3D on cpu and gpu.
 *
 * CellQuadratic only works in 2D on cpu.
 *
 * CellConservativeQuartic only works with ref ratio of 2 on cpu
 *
 * FaceDivFree works in 2D and 3D on cpu and gpu.
 * The algorithm is restricted to ref ratio of 2.
 */

//
// CONSTRUCT A GLOBAL OBJECT OF EACH VERSION.
//
PCInterp                  pc_interp;
NodeBilinear              node_bilinear_interp;
FaceLinear                face_linear_interp;
FaceDivFree               face_divfree_interp;
CellConservativeLinear    lincc_interp;
CellConservativeLinear    cell_cons_interp(0);
CellConservativeProtected protected_interp;
CellBilinear              cell_bilinear_interp;

#ifndef BL_NO_FORT
CellQuadratic             quadratic_interp;
CellConservativeQuartic   quartic_interp;
#endif

NodeBilinear::~NodeBilinear () {}

Box
NodeBilinear::CoarseBox (const Box& fine,
                         int        ratio)
{
    Box b = amrex::coarsen(fine,ratio);

    for (int i = 0; i < AMREX_SPACEDIM; i++)
    {
        if (b.length(i) < 2)
        {
            //
            // Don't want degenerate boxes.
            //
            b.growHi(i,1);
        }
    }

    return b;
}

Box
NodeBilinear::CoarseBox (const Box&     fine,
                         const IntVect& ratio)
{
    Box b = amrex::coarsen(fine,ratio);

    for (int i = 0; i < AMREX_SPACEDIM; i++)
    {
        if (b.length(i) < 2)
        {
            //
            // Don't want degenerate boxes.
            //
            b.growHi(i,1);
        }
    }

    return b;
}

void
NodeBilinear::interp (const FArrayBox&  crse,
                      int               crse_comp,
                      FArrayBox&        fine,
                      int               fine_comp,
                      int               ncomp,
                      const Box&        fine_region,
                      const IntVect&    ratio,
                      const Geometry& /*crse_geom */,
                      const Geometry& /*fine_geom */,
                      Vector<BCRec> const& /*bcr*/,
                      int               /*actual_comp*/,
                      int               /*actual_state*/,
                      RunOn             runon)
{
    BL_PROFILE("NodeBilinear::interp()");

    Array4<Real const> const& crsearr = crse.const_array();
    Array4<Real> const& finearr = fine.array();
    AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon,fine_region,ncomp,i,j,k,n,
    {
        mf_nodebilin_interp(i,j,k,n, finearr, fine_comp, crsearr, crse_comp, ratio);
    });
}

Box
FaceLinear::CoarseBox (const Box& fine, int ratio)
{
    return CoarseBox(fine, IntVect(ratio));
}

Box
FaceLinear::CoarseBox (const Box& fine, const IntVect& ratio)
{
    Box b = amrex::coarsen(fine,ratio);
    for (int i = 0; i < AMREX_SPACEDIM; i++) {
        if (b.type(i) == IndexType::NODE && b.length(i) < 2) {
            // Don't want degenerate boxes in nodal direction.
            b.growHi(i,1);
        }
    }
    return b;
}

void
FaceLinear::interp (const FArrayBox&  crse,
                    int               crse_comp,
                    FArrayBox&        fine,
                    int               fine_comp,
                    int               ncomp,
                    const Box&        fine_region,
                    const IntVect&    ratio,
                    const Geometry& /*crse_geom */,
                    const Geometry& /*fine_geom */,
                    Vector<BCRec> const& /*bcr*/,
                    int               /*actual_comp*/,
                    int               /*actual_state*/,
                    RunOn             runon)
{
    BL_PROFILE("FaceLinear::interp()");

    AMREX_ASSERT(AMREX_D_TERM(fine_region.type(0),+fine_region.type(1),+fine_region.type(2)) == 1);

    Array4<Real> const& fine_arr = fine.array(fine_comp);
    Array4<Real const> const& crse_arr = crse.const_array(crse_comp);

    if (fine_region.type(0) == IndexType::NODE)
    {
        AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon,fine_region,ncomp,i,j,k,n,
        {
            face_linear_interp_x(i,j,k,n,fine_arr,crse_arr,ratio);
        });
    }
#if (AMREX_SPACEDIM >= 2)
    else if (fine_region.type(1) == IndexType::NODE)
    {
        AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon,fine_region,ncomp,i,j,k,n,
        {
            face_linear_interp_y(i,j,k,n,fine_arr,crse_arr,ratio);
        });
    }
#if (AMREX_SPACEDIM == 3)
    else
    {
        AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon,fine_region,ncomp,i,j,k,n,
        {
            face_linear_interp_z(i,j,k,n,fine_arr,crse_arr,ratio);
        });
    }
#endif
#endif
}

void FaceLinear::interp_arr (Array<FArrayBox*, AMREX_SPACEDIM> const& crse,
                             const int         crse_comp,
                             Array<FArrayBox*, AMREX_SPACEDIM> const& fine,
                             const int         fine_comp,
                             const int         ncomp,
                             const Box&        fine_region,
                             const IntVect&    ratio,
                             Array<IArrayBox*, AMREX_SPACEDIM> const& /*solve_mask*/,
                             const Geometry&   /*crse_geom*/,
                             const Geometry&   /*fine_geom*/,
                             Vector<Array<BCRec, AMREX_SPACEDIM> > const& /*bcr*/,
                             const int         /*actual_comp*/,
                             const int         /*actual_state*/,
                             const RunOn       runon)
{
    BL_PROFILE("FaceLinear::interp_arr()");

    // cell centered -- relevant or guaranteed by caller?
    //AMREX_ASSERT(AMREX_D_TERM(fine_region.type(0),+fine_region.type(1),+fine_region.type(2)) == 1);

    Array<IndexType, AMREX_SPACEDIM> types;
    for (int d=0; d<AMREX_SPACEDIM; ++d)
        { types[d].set(d); }

    GpuArray<Array4<const Real>, AMREX_SPACEDIM> crse_arr;
    GpuArray<Array4<Real>, AMREX_SPACEDIM> fine_arr;
    for (int d=0; d<AMREX_SPACEDIM; ++d)
    {
        crse_arr[d] = crse[d]->const_array(crse_comp);
        fine_arr[d] = fine[d]->array(fine_comp);
    }

    AMREX_LAUNCH_HOST_DEVICE_LAMBDA_DIM_FLAG(runon,
              amrex::convert(fine_region,types[0]), bx0,
              {
                  AMREX_LOOP_3D(bx0, i, j, k,
                  {
                      for (int n=0; n<ncomp; ++n)
                      {
                          face_linear_interp_x(i,j,k,n,fine_arr[0],crse_arr[0],ratio);
                      }
                  });
              },
              amrex::convert(fine_region,types[1]), bx1,
              {
                  AMREX_LOOP_3D(bx1, i, j, k,
                  {
                      for (int n=0; n<ncomp; ++n)
                      {
                          face_linear_interp_y(i,j,k,n,fine_arr[1],crse_arr[1],ratio);
                      }
                  });
              },
              amrex::convert(fine_region,types[2]), bx2,
              {
                  AMREX_LOOP_3D(bx2, i, j, k,
                  {
                      for (int n=0; n<ncomp; ++n)
                      {
                          face_linear_interp_z(i,j,k,n,fine_arr[2],crse_arr[2],ratio);
                      }
                  });
              });
}

FaceLinear::~FaceLinear () {}

CellBilinear::~CellBilinear () {}

Box
CellBilinear::CoarseBox (const Box& fine, int ratio)
{
    return CoarseBox(fine, IntVect(ratio));
}

Box
CellBilinear::CoarseBox (const Box& fine, const IntVect& ratio)
{
    const int* lo = fine.loVect();
    const int* hi = fine.hiVect();

    Box crse(amrex::coarsen(fine,ratio));
    const int* clo = crse.loVect();
    const int* chi = crse.hiVect();

    for (int i = 0; i < AMREX_SPACEDIM; i++) {
        if ((lo[i]-clo[i]*ratio[i])*2 < ratio[i]) {
            crse.growLo(i,1);
        }
        if ((hi[i]-chi[i]*ratio[i])*2 >= ratio[i]) {
            crse.growHi(i,1);
        }
    }
    return crse;
}

void
CellBilinear::interp (const FArrayBox&  crsefab,
                      int               crse_comp,
                      FArrayBox&        finefab,
                      int               fine_comp,
                      int               ncomp,
                      const Box&        fine_region,
                      const IntVect &   ratio,
                      const Geometry& /*crse_geom*/,
                      const Geometry& /*fine_geom*/,
                      Vector<BCRec> const& /*bcr*/,
                      int               /*actual_comp*/,
                      int               /*actual_state*/,
                      RunOn             runon)
{
    BL_PROFILE("CellBilinear::interp()");

    auto const& crse = crsefab.const_array();
    auto const& fine = finefab.array();
    AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon,fine_region,ncomp,i,j,k,n,
    {
        mf_cell_bilin_interp(i,j,k,n, fine, fine_comp, crse, crse_comp, ratio);
    });
}


CellConservativeLinear::CellConservativeLinear (bool do_linear_limiting_)
{
    do_linear_limiting = do_linear_limiting_;
}

CellConservativeLinear::~CellConservativeLinear ()
{}

Box
CellConservativeLinear::CoarseBox (const Box&     fine,
                                   const IntVect& ratio)
{
    Box crse = amrex::coarsen(fine,ratio);
    crse.grow(1);
    return crse;
}

Box
CellConservativeLinear::CoarseBox (const Box& fine,
                                   int        ratio)
{
    Box crse(amrex::coarsen(fine,ratio));
    crse.grow(1);
    return crse;
}

void
CellConservativeLinear::interp (const FArrayBox& crse,
                                int              crse_comp,
                                FArrayBox&       fine,
                                int              fine_comp,
                                int              ncomp,
                                const Box&       fine_region,
                                const IntVect&   ratio,
                                const Geometry&  crse_geom,
                                const Geometry&  fine_geom,
                                Vector<BCRec> const& bcr,
                                int              /*actual_comp*/,
                                int              /*actual_state*/,
                                RunOn             runon)
{
    BL_PROFILE("CellConservativeLinear::interp()");
    BL_ASSERT(bcr.size() >= ncomp);

    AMREX_ASSERT(fine.box().contains(fine_region));

    bool run_on_gpu = (runon == RunOn::Gpu && Gpu::inLaunchRegion());

    Box const& cdomain = crse_geom.Domain();
    amrex::ignore_unused(fine_geom);

    Array4<Real const> const& crsearr = crse.const_array();
    Array4<Real> const& finearr = fine.array();

    const Box& crse_region = CoarseBox(fine_region,ratio);
    const Box& cslope_bx = amrex::grow(crse_region,-1);

    AsyncArray<BCRec> async_bcr(bcr.data(), (run_on_gpu) ? ncomp : 0);
    BCRec const* bcrp = (run_on_gpu) ? async_bcr.data() : bcr.data();

    FArrayBox ccfab(cslope_bx, ncomp*AMREX_SPACEDIM);
    Elixir cceli;
    if (run_on_gpu) cceli = ccfab.elixir();
    Array4<Real> const& tmp = ccfab.array();
    Array4<Real const> const& ctmp = ccfab.const_array();

#if (AMREX_SPACEDIM == 1)
    if (crse_geom.IsSPHERICAL()) {
        Real drf = fine_geom.CellSize(0);
        Real rlo = fine_geom.Offset(0);
        if (do_linear_limiting) {
            AMREX_HOST_DEVICE_PARALLEL_FOR_3D_FLAG(runon, cslope_bx, i, j, k,
            {
                mf_cell_cons_lin_interp_llslope(i,j,k, tmp, crsearr, crse_comp, ncomp,
                                                cdomain, bcrp);
            });
        } else {
            AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon, cslope_bx, ncomp, i, j, k, n,
            {
                amrex::ignore_unused(j,k);
                mf_cell_cons_lin_interp_mcslope_sph(i, n, tmp, crsearr, crse_comp, ncomp,
                                                    cdomain, ratio, bcrp, drf, rlo);
            });
        }

        AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon, fine_region, ncomp, i, j, k, n,
        {
            amrex::ignore_unused(j,k);
            mf_cell_cons_lin_interp_sph(i, n, finearr, fine_comp, ctmp,
                                        crsearr, crse_comp, ncomp, ratio, drf, rlo);
        });
    } else
#elif (AMREX_SPACEDIM == 2)
    if (crse_geom.IsRZ()) {
        Real drf = fine_geom.CellSize(0);
        Real rlo = fine_geom.Offset(0);
        if (do_linear_limiting) {
            AMREX_HOST_DEVICE_PARALLEL_FOR_3D_FLAG(runon, cslope_bx, i, j, k,
            {
                mf_cell_cons_lin_interp_llslope(i,j,k, tmp, crsearr, crse_comp, ncomp,
                                                cdomain, bcrp);
            });
        } else {
            AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon, cslope_bx, ncomp, i, j, k, n,
            {
                amrex::ignore_unused(k);
                mf_cell_cons_lin_interp_mcslope_rz(i, j, n, tmp, crsearr, crse_comp, ncomp,
                                                   cdomain, ratio, bcrp, drf, rlo);
            });
        }

        AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon, fine_region, ncomp, i, j, k, n,
        {
            amrex::ignore_unused(k);
            mf_cell_cons_lin_interp_rz(i, j, n, finearr, fine_comp, ctmp,
                                       crsearr, crse_comp, ncomp, ratio, drf, rlo);
        });
    } else
#endif
    {
        if (do_linear_limiting) {
            AMREX_HOST_DEVICE_PARALLEL_FOR_3D_FLAG(runon, cslope_bx, i, j, k,
            {
                mf_cell_cons_lin_interp_llslope(i,j,k, tmp, crsearr, crse_comp, ncomp,
                                                cdomain, bcrp);
            });
        } else {
            AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon, cslope_bx, ncomp, i, j, k, n,
            {
                mf_cell_cons_lin_interp_mcslope(i,j,k,n, tmp, crsearr, crse_comp, ncomp,
                                                cdomain, ratio, bcrp);
            });
        }

        AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon, fine_region, ncomp, i, j, k, n,
        {
            mf_cell_cons_lin_interp(i,j,k,n, finearr, fine_comp, ctmp,
                                    crsearr, crse_comp, ncomp, ratio);
        });
    }
}

#ifndef BL_NO_FORT
CellQuadratic::CellQuadratic (bool limit)
{
    do_limited_slope = limit;
}

CellQuadratic::~CellQuadratic () {}

Box
CellQuadratic::CoarseBox (const Box&     fine,
                          const IntVect& ratio)
{
    Box crse = amrex::coarsen(fine,ratio);
    crse.grow(1);
    return crse;
}

Box
CellQuadratic::CoarseBox (const Box& fine,
                          int        ratio)
{
    Box crse = amrex::coarsen(fine,ratio);
    crse.grow(1);
    return crse;
}

void
CellQuadratic::interp (const FArrayBox& crse,
                       int              crse_comp,
                       FArrayBox&       fine,
                       int              fine_comp,
                       int              ncomp,
                       const Box&       fine_region,
                       const IntVect&   ratio,
                       const Geometry&  crse_geom,
                       const Geometry&  fine_geom,
                       Vector<BCRec> const&  bcr,
                       int              actual_comp,
                       int              actual_state,
                       RunOn            /*runon*/)
{
#if (AMREX_SPACEDIM == 1)
    amrex::ignore_unused(crse,crse_comp,fine,fine_comp,ncomp,fine_region,
                         ratio,crse_geom,fine_geom,bcr,actual_comp,actual_state);
    amrex::Abort("1D CellQuadratic::interp not supported");
#else
    BL_PROFILE("CellQuadratic::interp()");
    BL_ASSERT(bcr.size() >= ncomp);
    //
    // Make box which is intersection of fine_region and domain of fine.
    //
    Box target_fine_region = fine_region & fine.box();

    Box crse_bx(amrex::coarsen(target_fine_region,ratio));
    Box fslope_bx(amrex::refine(crse_bx,ratio));
    Box cslope_bx(crse_bx);
    cslope_bx.grow(1);
    BL_ASSERT(crse.box().contains(cslope_bx));
    //
    // Alloc temp space for coarse grid slopes: here we use 5
    // instead of AMREX_SPACEDIM because of the x^2, y^2 and xy terms
    //
    long t_long = cslope_bx.numPts();
    BL_ASSERT(t_long < INT_MAX);
    int c_len = int(t_long);

    Vector<Real> cslope(5*c_len);

    int loslp = cslope_bx.index(crse_bx.smallEnd());
    int hislp = cslope_bx.index(crse_bx.bigEnd());

    t_long = cslope_bx.numPts();
    BL_ASSERT(t_long < INT_MAX);
    int cslope_vol = int(t_long);
    int clo        = 1 - loslp;
    int chi        = clo + cslope_vol - 1;
    c_len          = hislp - loslp + 1;
    //
    // Alloc temp space for one strip of fine grid slopes: here we use 5
    // instead of AMREX_SPACEDIM because of the x^2, y^2 and xy terms.
    //
    int dir;
    int f_len = fslope_bx.longside(dir);

    Vector<Real> strip((5+2)*f_len);

    Real* fstrip = strip.dataPtr();
    Real* foff   = fstrip + f_len;
    Real* fslope = foff + f_len;
    //
    // Get coarse and fine edge-centered volume coordinates.
    //
    Vector<Real> fvc[AMREX_SPACEDIM];
    Vector<Real> cvc[AMREX_SPACEDIM];
    for (dir = 0; dir < AMREX_SPACEDIM; dir++)
    {
        fine_geom.GetEdgeVolCoord(fvc[dir],target_fine_region,dir);
        crse_geom.GetEdgeVolCoord(cvc[dir],crse_bx,dir);
    }
    //
    // Alloc tmp space for slope calc and to allow for vectorization.
    //
    Real* fdat        = fine.dataPtr(fine_comp);
    const Real* cdat  = crse.dataPtr(crse_comp);
    const int* flo    = fine.loVect();
    const int* fhi    = fine.hiVect();
    const int* fblo   = target_fine_region.loVect();
    const int* fbhi   = target_fine_region.hiVect();
    const int* cblo   = crse_bx.loVect();
    const int* cbhi   = crse_bx.hiVect();
    const int* fslo   = fslope_bx.loVect();
    const int* fshi   = fslope_bx.hiVect();
    int slope_flag    = (do_limited_slope ? 1 : 0);
    Vector<int> bc     = GetBCArray(bcr);
    const int* ratioV = ratio.getVect();

    amrex_cqinterp (fdat,AMREX_ARLIM(flo),AMREX_ARLIM(fhi),
                   AMREX_ARLIM(fblo), AMREX_ARLIM(fbhi),
                   &ncomp,AMREX_D_DECL(&ratioV[0],&ratioV[1],&ratioV[2]),
                   cdat,&clo,&chi,
                   AMREX_ARLIM(cblo), AMREX_ARLIM(cbhi),
                   fslo,fshi,
                   cslope.dataPtr(),&c_len,fslope,fstrip,&f_len,foff,
                   bc.dataPtr(), &slope_flag,
                   AMREX_D_DECL(fvc[0].dataPtr(),fvc[1].dataPtr(),fvc[2].dataPtr()),
                   AMREX_D_DECL(cvc[0].dataPtr(),cvc[1].dataPtr(),cvc[2].dataPtr()),
                   &actual_comp,&actual_state);

#endif /*(AMREX_SPACEDIM > 1)*/
}
#endif


PCInterp::~PCInterp () {}

Box
PCInterp::CoarseBox (const Box& fine,
                     int        ratio)
{
    return amrex::coarsen(fine,ratio);
}

Box
PCInterp::CoarseBox (const Box&     fine,
                     const IntVect& ratio)
{
    return amrex::coarsen(fine,ratio);
}

void
PCInterp::interp (const FArrayBox& crse,
                  int              crse_comp,
                  FArrayBox&       fine,
                  int              fine_comp,
                  int              ncomp,
                  const Box&       fine_region,
                  const IntVect&   ratio,
                  const Geometry& /*crse_geom*/,
                  const Geometry& /*fine_geom*/,
                  Vector<BCRec> const& /*bcr*/,
                  int               /*actual_comp*/,
                  int               /*actual_state*/,
                  RunOn             runon)
{
    BL_PROFILE("PCInterp::interp()");

    Array4<Real const> const& crsearr = crse.const_array();
    Array4<Real> const& finearr = fine.array();;

    AMREX_LAUNCH_HOST_DEVICE_LAMBDA_FLAG ( runon, fine_region, tbx,
    {
        amrex::pcinterp_interp(tbx,finearr,fine_comp,ncomp,crsearr,crse_comp,ratio);
    });
}

CellConservativeProtected::CellConservativeProtected ()
    : CellConservativeLinear(true) {}

CellConservativeProtected::~CellConservativeProtected () {}

void
CellConservativeProtected::protect (const FArrayBox& /*crse*/,
                                    int              /*crse_comp*/,
                                    FArrayBox&       fine,
                                    int              /*fine_comp*/,
                                    FArrayBox&       fine_state,
                                    int              /*state_comp*/,
                                    int              ncomp,
                                    const Box&       fine_region,
                                    const IntVect&   ratio,
                                    const Geometry&  crse_geom,
                                    const Geometry&  fine_geom,
                                    Vector<BCRec>&   bcr,
                                    RunOn            runon)
{
#if (AMREX_SPACEDIM == 1)
    amrex::ignore_unused(fine,fine_state,
                         ncomp,fine_region,ratio,
                         crse_geom,fine_geom,bcr,runon);
    amrex::Abort("1D CellConservativeProtected::protect not supported");
#else
    BL_PROFILE("CellConservativeProtected::protect()");
    BL_ASSERT(bcr.size() >= ncomp);
    amrex::ignore_unused(bcr);

    //
    // Make box which is intersection of fine_region and domain of fine.
    //
    Box target_fine_region = fine_region & fine.box();

    //
    // crse_bx is coarsening of target_fine_region, grown by 1.
    //
    Box crse_bx = CoarseBox(target_fine_region,ratio);

    //
    // cs_bx is coarsening of target_fine_region.
    //
    Box cs_bx(crse_bx);
    cs_bx.grow(-1);

#if (AMREX_SPACEDIM == 2)
    /*
     * Get coarse and fine geometry data.
     */
    GeometryData cs_geomdata = crse_geom.data();
    GeometryData fn_geomdata = fine_geom.data();
#else
    amrex::ignore_unused(crse_geom, fine_geom);
#endif

    // Extract box from fine fab
    const Box& fnbx = fine.box();

    // Extract pointers to fab data
    Array4<Real>       const&   fnarr = fine.array();
    Array4<Real const> const& fnstarr = fine_state.const_array();

    /*
     * Loop over coarse indices.
     */
#if (AMREX_SPACEDIM == 2)
    AMREX_HOST_DEVICE_PARALLEL_FOR_3D_FLAG(runon, cs_bx, ic, jc, kc,
    {
        ccprotect_2d(ic, jc, kc, ncomp,
                     fnbx, ratio,
                     cs_geomdata, fn_geomdata,
                     fnarr, fnstarr);
    }); // cs_bx
#else
    AMREX_HOST_DEVICE_PARALLEL_FOR_3D_FLAG(runon, cs_bx, ic, jc, kc,
    {
        ccprotect_3d(ic, jc, kc, ncomp,
                     fnbx, ratio,
                     fnarr, fnstarr);
    }); // cs_bx
#endif

#endif /*(AMREX_SPACEDIM == 1)*/

}

#ifndef BL_NO_FORT
CellConservativeQuartic::~CellConservativeQuartic () {}

Box
CellConservativeQuartic::CoarseBox (const Box& fine,
                                    int        ratio)
{
    Box crse(amrex::coarsen(fine,ratio));
    crse.grow(2);
    return crse;
}

Box
CellConservativeQuartic::CoarseBox (const Box&     fine,
                                    const IntVect& ratio)
{
    Box crse = amrex::coarsen(fine,ratio);
    crse.grow(2);
    return crse;
}

void
CellConservativeQuartic::interp (const FArrayBox&  crse,
                                 int               crse_comp,
                                 FArrayBox&        fine,
                                 int               fine_comp,
                                 int               ncomp,
                                 const Box&        fine_region,
                                 const IntVect&    ratio,
                                 const Geometry&   /* crse_geom */,
                                 const Geometry&   /* fine_geom */,
                                 Vector<BCRec> const&   bcr,
                                 int               actual_comp,
                                 int               actual_state,
                                 RunOn             /*runon*/)
{
    BL_PROFILE("CellConservativeQuartic::interp()");
    BL_ASSERT(bcr.size() >= ncomp);
    BL_ASSERT(ratio[0]==2);
#if (AMREX_SPACEDIM >= 2)
    BL_ASSERT(ratio[0] == ratio[1]);
#endif
#if (AMREX_SPACEDIM == 3)
    BL_ASSERT(ratio[1] == ratio[2]);
#endif

    //
    // Make box which is intersection of fine_region and domain of fine.
    //
    Box target_fine_region = fine_region & fine.box();
    //
    // crse_bx is coarsening of target_fine_region, grown by 2.
    //
    Box crse_bx = CoarseBox(target_fine_region,ratio);

    Box crse_bx2(crse_bx);
    crse_bx2.grow(-2);
    Box fine_bx2 = amrex::refine(crse_bx2,ratio);

    Real* fdat       = fine.dataPtr(fine_comp);
    const Real* cdat = crse.dataPtr(crse_comp);

    const int* flo    = fine.loVect();
    const int* fhi    = fine.hiVect();
    const int* clo    = crse.loVect();
    const int* chi    = crse.hiVect();
    const int* fblo   = target_fine_region.loVect();
    const int* fbhi   = target_fine_region.hiVect();
    const int* cblo   = crse_bx.loVect();
    const int* cbhi   = crse_bx.hiVect();
    const int* cb2lo  = crse_bx2.loVect();
    const int* cb2hi  = crse_bx2.hiVect();
    const int* fb2lo  = fine_bx2.loVect();
    const int* fb2hi  = fine_bx2.hiVect();

    Vector<int> bc     = GetBCArray(bcr);
    const int* ratioV = ratio.getVect();

    int ltmp = fb2hi[0]-fb2lo[0]+1;
    Vector<Real> ftmp(ltmp);

#if (AMREX_SPACEDIM >= 2)
    ltmp = (cbhi[0]-cblo[0]+1)*ratio[1];
    Vector<Real> ctmp(ltmp);
#endif

#if (AMREX_SPACEDIM == 3)
    ltmp = (cbhi[0]-cblo[0]+1)*(cbhi[1]-cblo[1]+1)*ratio[2];
    Vector<Real> ctmp2(ltmp);
#endif

    amrex_quartinterp (fdat,AMREX_ARLIM(flo),AMREX_ARLIM(fhi),
                       fblo, fbhi, fb2lo, fb2hi,
                       cdat,AMREX_ARLIM(clo),AMREX_ARLIM(chi),
                       cblo, cbhi, cb2lo, cb2hi,
                       &ncomp,
                       AMREX_D_DECL(&ratioV[0],&ratioV[1],&ratioV[2]),
                       AMREX_D_DECL(ftmp.dataPtr(), ctmp.dataPtr(), ctmp2.dataPtr()),
                       bc.dataPtr(),&actual_comp,&actual_state);
}
#endif

FaceDivFree::~FaceDivFree () {}

Box
FaceDivFree::CoarseBox (const Box& fine,
                        int        ratio)
{
    Box b = amrex::coarsen(fine,ratio).grow(1);
    return b;
}

Box
FaceDivFree::CoarseBox (const Box&     fine,
                        const IntVect& ratio)
{
    Box b = amrex::coarsen(fine,ratio).grow(1);
    return b;
}

void
FaceDivFree::interp (const FArrayBox&  /*crse*/,
                     int               /*crse_comp*/,
                     FArrayBox&        /*fine*/,
                     int               /*fine_comp*/,
                     int               /*ncomp*/,
                     const Box&        /*fine_region*/,
                     const IntVect&    /*ratio*/,
                     const Geometry&   /*crse_geom*/,
                     const Geometry&   /*fine_geom*/,
                     Vector<BCRec> const& /*bcr*/,
                     int               /*actual_comp*/,
                     int               /*actual_state*/,
                     RunOn             /*runon*/)
{
    amrex::Abort("FaceDivFree does not work on a single MultiFab. Call 'interp_arr' instead.");
}

void
FaceDivFree::interp_arr (Array<FArrayBox*, AMREX_SPACEDIM> const& crse,
                         const int         crse_comp,
                         Array<FArrayBox*, AMREX_SPACEDIM> const& fine,
                         const int         fine_comp,
                         const int         ncomp,
                         const Box&        fine_region,
                         const IntVect&    ratio,
                         Array<IArrayBox*, AMREX_SPACEDIM> const& solve_mask,
                         const Geometry&   /*crse_geom */,
                         const Geometry&   fine_geom,
                         Vector<Array<BCRec, AMREX_SPACEDIM> > const& /*bcr*/,
                         const int         /*actual_comp*/,
                         const int         /*actual_state*/,
                         const RunOn       runon)
{
    BL_PROFILE("FaceDivFree::interp()");

    Array<IndexType, AMREX_SPACEDIM> types;
    for (int d=0; d<AMREX_SPACEDIM; ++d)
        { types[d].set(d); }

    // This is currently only designed for octree, where ratio = 2.
    AMREX_ALWAYS_ASSERT(ratio == 2);

    const Box c_fine_region = amrex::coarsen(fine_region, ratio);
    GpuArray<Real, AMREX_SPACEDIM> cell_size = fine_geom.CellSizeArray();

    GpuArray<Array4<const Real>, AMREX_SPACEDIM> crsearr;
    GpuArray<Array4<Real>, AMREX_SPACEDIM> finearr;
    GpuArray<Array4<const int>, AMREX_SPACEDIM> maskarr;
    for (int d=0; d<AMREX_SPACEDIM; ++d)
    {
        crsearr[d] = crse[d]->const_array(crse_comp);
        finearr[d] = fine[d]->array(fine_comp);
        if (solve_mask[d] != nullptr)
            { maskarr[d] = solve_mask[d]->const_array(0); }
    }

    // Fuse the launches, 1 for each dimension, into a single launch.
    AMREX_LAUNCH_HOST_DEVICE_LAMBDA_DIM_FLAG(runon,
              amrex::convert(c_fine_region,types[0]), bx0,
              {
                  AMREX_LOOP_3D(bx0, i, j, k,
                  {
                      for (int n=0; n<ncomp; ++n)
                      {
                          amrex::facediv_face_interp<Real> (i,j,k,crse_comp+n,fine_comp+n, 0,
                                                            crsearr[0], finearr[0], maskarr[0], ratio);
                      }
                  });
              },
              amrex::convert(c_fine_region,types[1]), bx1,
              {
                  AMREX_LOOP_3D(bx1, i, j, k,
                  {
                      for (int n=0; n<ncomp; ++n)
                      {
                          amrex::facediv_face_interp<Real> (i,j,k,crse_comp+n,fine_comp+n, 1,
                                                            crsearr[1], finearr[1], maskarr[1], ratio);
                      }
                  });
              },
              amrex::convert(c_fine_region,types[2]), bx2,
              {
                  AMREX_LOOP_3D(bx2, i, j, k,
                  {
                      for (int n=0; n<ncomp; ++n)
                      {
                          amrex::facediv_face_interp<Real> (i,j,k,crse_comp+n,fine_comp+n, 2,
                                                            crsearr[2], finearr[2], maskarr[2], ratio);
                      }
                  });
              });

    AMREX_HOST_DEVICE_PARALLEL_FOR_4D_FLAG(runon,c_fine_region,ncomp,i,j,k,n,
    {
        amrex::facediv_int<Real>(i, j, k, fine_comp+n, finearr, ratio, cell_size);
    });
}

}
