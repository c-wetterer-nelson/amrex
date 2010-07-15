//
// $Id: Amr.cpp,v 1.216 2010-07-15 04:01:29 almgren Exp $
//
#include <winstd.H>

#include <algorithm>
#include <cstdio>
#include <list>
#if (defined(BL_USE_MPI) && ! defined(BL_USEOLDREADS))
#include <iostream>
#include <strstream>
#endif

#include <Geometry.H>
#include <TagBox.H>
#include <Array.H>
#include <CoordSys.H>
#include <ParmParse.H>
#include <BoxDomain.H>
#include <Cluster.H>
#include <LevelBld.H>
#include <AmrLevel.H>
#include <PROB_AMR_F.H>
#include <Amr.H>
#include <ParallelDescriptor.H>
#include <Profiler.H>
#include <Utility.H>
#include <DistributionMapping.H>
#include <FabSet.H>

#ifdef BL_USE_ARRAYVIEW
#include <DatasetClient.H>
#endif
//
// This MUST be defined if don't have pubsetbuf() in I/O Streams Library.
//
#ifdef BL_USE_SETBUF
#define pubsetbuf setbuf
#endif
//
// Static class members.
//
std::list<std::string> Amr::state_plot_vars;
std::list<std::string> Amr::derive_plot_vars;
bool                   Amr::first_plotfile = true;

namespace
{
  bool plot_files_output              = true;
  bool checkpoint_files_output        = true;
  bool refine_grid_layout             = true;
  int  plot_nfiles                    = 64;
  int  checkpoint_nfiles              = 64;
  int  mffile_nstreams                = 1;
  int  probinit_natonce               = 32;
  const std::string CheckPointVersion = "CheckPointVersion_1.0";
  int regrid_on_restart               = 0;
  int plotfile_on_restart             = 0;
  int checkpoint_on_restart           = 0;
  int compute_new_dt_on_regrid        = 0;
}

bool Amr::Plot_Files_Output () { return plot_files_output; }

std::ostream&
Amr::DataLog (int i)
{
    return datalog[i];
}

bool
Amr::RegridOnRestart () const
{
    return regrid_on_restart;
}

int
Amr::checkInt () const
{
    return check_int;
}

Real
Amr::checkPer () const
{
    return check_per;
}

int
Amr::plotInt () const
{
    return plot_int;
}

Real
Amr::plotPer () const
{
    return plot_per;
}

const std::list<std::string>&
Amr::statePlotVars ()
{
    return state_plot_vars;
}

const std::list<std::string>&
Amr::derivePlotVars ()
{
    return derive_plot_vars;
}

int
Amr::blockingFactor (int lev) const
{
    return blocking_factor[lev];
}


int
Amr::maxGridSize (int lev) const
{
    return max_grid_size[lev];
}

int
Amr::maxLevel () const
{
    return max_level;
}

int
Amr::finestLevel () const
{
    return finest_level;
}

IntVect
Amr::refRatio (int level) const
{
    return ref_ratio[level];
}

int
Amr::nCycle (int level) const
{
    return n_cycle[level];
}

const Array<IntVect>&
Amr::refRatio () const
{
    return ref_ratio;
}

Real
Amr::dtLevel (int level) const
{
    return dt_level[level];
}

const Array<Real>&
Amr::dtLevel () const
{
    return dt_level;
}

const Geometry&
Amr::Geom (int level) const
{
    return geom[level];
}

int
Amr::levelSteps (int i) const
{
    return level_steps[i];
}

int
Amr::levelCount (int i) const
{
    return level_count[i];
}

Real
Amr::cumTime () const
{
    return cumtime;
}

int
Amr::regridInt (int lev) const
{
    return regrid_int[lev];
}

int
Amr::nErrorBuf (int lev) const
{
    return n_error_buf[lev];
}

Real
Amr::gridEff () const
{
    return grid_eff;
}

int
Amr::subCycle () const
{
    return sub_cycle;
}

int
Amr::nProper () const
{
    return n_proper;
}

const std::string&
Amr::theRestartFile () const
{
    return restart_file;
}

void
Amr::setDtMin (const Array<Real>& dt_min_in)
{
    for (int i = 0; i <= finest_level; i++)
        dt_min[i] = dt_min_in[i];
}

AmrLevel&
Amr::getLevel (int lev)
{
    return amr_level[lev];
}

PArray<AmrLevel>&
Amr::getAmrLevels ()
{
    return amr_level;
}

long
Amr::cellCount (int lev)
{
    return amr_level[lev].countCells();
}

int
Amr::numGrids (int lev)
{
    return amr_level[lev].numGrids();
}

const BoxArray&
Amr::boxArray (int lev) const
{
    return amr_level[lev].boxArray();
}

MultiFab*
Amr::derive (const std::string& name,
             Real               time,
             int                lev,
             int                ngrow)
{
    return amr_level[lev].derive(name,time,ngrow);
}

int
Amr::MaxRefRatio (int level) const
{
    int maxval = 0;
    for (int n = 0; n<BL_SPACEDIM; n++) 
        maxval = std::max(maxval,ref_ratio[level][n]);
    return maxval;
}

Amr::Amr ()
    :
    amr_level(PArrayManage)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::Amr()");
    //
    // Setup Geometry from ParmParse file.
    // May be needed for variableSetup or even getLevelBld.
    //
    Geometry::Setup();
    //
    // Determine physics class.
    //
    levelbld = getLevelBld();
    //
    // Global function that define state variables.
    //
    levelbld->variableSetUp();
    //
    // Set default values.
    //
    max_level        = -1;
    record_run_info  = false;
    record_run_info_terse  = false;
    record_grid_info = false;
    grid_eff         = 0.7;
    last_checkpoint  = 0;
    last_plotfile    = 0;
    plot_int         = -1;
    n_proper         = 1;

    // This is the default for the format of plotfile and checkpoint names.
    file_name_digits = 5;

    int i;
    for (i = 0; i < BL_SPACEDIM; i++)
        isPeriodic[i] = false;

    ParmParse pp("amr");
    //
    // Check for command line flags.
    //
    verbose = 0;
    pp.query("v",verbose);

    pp.query("regrid_on_restart",regrid_on_restart);
    pp.query("plotfile_on_restart",plotfile_on_restart);
    pp.query("checkpoint_on_restart",checkpoint_on_restart);

    pp.query("compute_new_dt_on_regrid",compute_new_dt_on_regrid);

    pp.query("checkpoint_files_output", checkpoint_files_output);
    pp.query("plot_files_output", plot_files_output);

    pp.query("plot_nfiles", plot_nfiles);
    pp.query("checkpoint_nfiles", checkpoint_nfiles);
    //
    // -1 ==> use ParallelDescriptor::NProcs().
    //
    if (plot_nfiles       == -1) plot_nfiles       = ParallelDescriptor::NProcs();
    if (checkpoint_nfiles == -1) checkpoint_nfiles = ParallelDescriptor::NProcs();

    pp.query("refine_grid_layout", refine_grid_layout);

    pp.query("mffile_nstreams", mffile_nstreams);
    pp.query("probinit_natonce", probinit_natonce);
    probinit_natonce = std::max(1, std::min(ParallelDescriptor::NProcs(), probinit_natonce));

    pp.query("file_name_digits", file_name_digits);

    sub_cycle = true;
    if (pp.contains("nosub"))
        sub_cycle = false;

    pp.query("regrid_file",grids_file);
    if (pp.contains("run_log"))
    {
        std::string log_file_name;
        pp.get("run_log",log_file_name);
        setRecordRunInfo(log_file_name);
    }
    if (pp.contains("run_log_terse"))
    {
        std::string log_file_name;
        pp.get("run_log_terse",log_file_name);
        setRecordRunInfoTerse(log_file_name);
    }
    if (pp.contains("grid_log"))
    {
        std::string grid_file_name;
        pp.get("grid_log",grid_file_name);
        setRecordGridInfo(grid_file_name);
    }

    if (pp.contains("data_log"))
    {
      int num_datalogs = pp.countval("data_log");
      datalog.resize(num_datalogs);
      Array<std::string> data_file_names(num_datalogs);
      pp.queryarr("data_log",data_file_names,0,num_datalogs);
      for (int i = 0; i < num_datalogs; i++) 
        setRecordDataInfo(i,data_file_names[i]);
    }

    probin_file = "probin";  // Make "probin" the default

    if (pp.contains("probin_file"))
    {
        pp.get("probin_file",probin_file);
    }
    //
    // Restart or run from scratch?
    //
    pp.query("restart", restart_file);
    //
    // Read max_level and alloc memory for container objects.
    //
    pp.get("max_level", max_level);
    int nlev     = max_level+1;
    geom.resize(nlev);
    dt_level.resize(nlev);
    level_steps.resize(nlev);
    level_count.resize(nlev);
    n_cycle.resize(nlev);
    dt_min.resize(nlev);
    blocking_factor.resize(nlev);
    max_grid_size.resize(nlev);
    n_error_buf.resize(nlev);
    amr_level.resize(nlev);
    //
    // Set bogus values.
    //
    for (i = 0; i < nlev; i++)
    {
        dt_level[i]    = 1.e200; // Something nonzero so old & new will differ
        level_steps[i] = 0;
        level_count[i] = 0;
        n_cycle[i]     = 0;
        dt_min[i]      = 0.0;
        n_error_buf[i] = 1;
        blocking_factor[i] = 2;
        max_grid_size[i] = (BL_SPACEDIM == 2) ? 128 : 32;
    }

    if (max_level > 0) 
    {
       regrid_int.resize(max_level);
       for (i = 0; i < max_level; i++)
           regrid_int[i]  = 0;
    }

    ref_ratio.resize(max_level);
    for (i = 0; i < max_level; i++)
        ref_ratio[i] = IntVect::TheZeroVector();
    //
    // Read other amr specific values.
    //
    check_file_root = "chk";
    pp.query("check_file",check_file_root);

    check_int = -1;
    int got_check_int = pp.query("check_int",check_int);

    check_per = -1.0;
    int got_check_per = pp.query("check_per",check_per);

    if (got_check_int == 1 && got_check_per == 1)
    {
        BoxLib::Error("Must only specify amr.check_int OR amr.check_per");
    }

    plot_file_root = "plt";
    pp.query("plot_file",plot_file_root);

    plot_int = -1;
    int got_plot_int = pp.query("plot_int",plot_int);

    plot_per = -1.0;
    int got_plot_per = pp.query("plot_per",plot_per);

    if (got_plot_int == 1 && got_plot_per == 1)
    {
        BoxLib::Error("Must only specify amr.plot_int OR amr.plot_per");
    }

    pp.query("n_proper",n_proper);
    pp.query("grid_eff",grid_eff);
    pp.queryarr("n_error_buf",n_error_buf,0,max_level);

    //
    // Read in the refinement ratio IntVects as integer BL_SPACEDIM-tuples.
    //
    if (max_level > 0)
    {
        const int nratios_vect = max_level*BL_SPACEDIM;

        Array<int> ratios_vect(nratios_vect);

        int got_vect = pp.queryarr("ref_ratio_vect",ratios_vect,0,nratios_vect);

        Array<int> ratios(max_level);

        const int got_int = pp.queryarr("ref_ratio",ratios,0,max_level);
   
        if (got_int == 1 && got_vect == 1 && ParallelDescriptor::IOProcessor())
        {
            BoxLib::Warning("Only input *either* ref_ratio or ref_ratio_vect");
        }
        else if (got_vect == 1)
        {
            int k = 0;
            for (i = 0; i < max_level; i++)
            {
                for (int n = 0; n < BL_SPACEDIM; n++,k++)
                    ref_ratio[i][n] = ratios_vect[k];
            }
        }
        else if (got_int == 1)
        {
            for (i = 0; i < max_level; i++)
            {
                for (int n = 0; n < BL_SPACEDIM; n++)
                    ref_ratio[i][n] = ratios[i];
            }
        }
        else
        {
            BoxLib::Error("Must input *either* ref_ratio or ref_ratio_vect");
        }
    }

    //
    // Read in max_grid_size..
    //
    if (pp.countval("max_grid_size") == 1)
    {
        //
        // Set all values to the single available value.
        //
        int the_max_grid_size = 0;

        pp.query("max_grid_size",the_max_grid_size);

        for (i = 0; i <= max_level; i++)
        {
            max_grid_size[i] = the_max_grid_size;
        }
    } else
    {
        //
        // Otherwise we expect a vector of max_grid_size values.
        //
        pp.queryarr("max_grid_size",max_grid_size,0,max_level);
    }

    //
    // Read in the blocking_factors.
    //
    if (pp.countval("blocking_factor") == 1)
    {
        //
        // Set all values to the single available value.
        //
        int the_blocking_factor = 0;

        pp.query("blocking_factor",the_blocking_factor);

        for (i = 0; i <= max_level; i++)
        {
            blocking_factor[i] = the_blocking_factor;
        }
    }
    else
    {
        //
        // Otherwise we expect a vector of blocking factors.
        //
        pp.queryarr("blocking_factor",blocking_factor,0,max_level);
    }

    //
    // Read in the regrid interval if max_level > 0.
    //
    if (max_level > 0) 
    {
       int numvals = pp.countval("regrid_int");
       if (numvals == 1)
       {
           //
           // Set all values to the single available value.
           //
           int the_regrid_int = 0;
           pp.query("regrid_int",the_regrid_int);
           for (i = 0; i < max_level; i++)
           {
               regrid_int[i] = the_regrid_int;
           }
       }
       else if (numvals < max_level)
       {
           BoxLib::Error("You did not specify enough values of regrid_int");
       }
       else 
       {
           //
           // Otherwise we expect a vector of max_level values
        //
           pp.queryarr("regrid_int",regrid_int,0,max_level);
       }
    }

    //
    // Read computational domain and set geometry.
    //
    Array<int> n_cell(BL_SPACEDIM);
    pp.getarr("n_cell",n_cell,0,BL_SPACEDIM);
    BL_ASSERT(n_cell.size() == BL_SPACEDIM);
    IntVect lo(IntVect::TheZeroVector()), hi(n_cell);
    hi -= IntVect::TheUnitVector();
    Box index_domain(lo,hi);
    for (i = 0; i <= max_level; i++)
    {
        geom[i].define(index_domain);
        if (i < max_level)
            index_domain.refine(ref_ratio[i]);
    }
    //
    // Now define offset for CoordSys.
    //
    Real offset[BL_SPACEDIM];
    for (i = 0; i < BL_SPACEDIM; i++)
    {
        const Real delta = Geometry::ProbLength(i)/(Real)n_cell[i];
        offset[i]        = Geometry::ProbLo(i) + delta*lo[i];
    }
    CoordSys::SetOffset(offset);
}

bool
Amr::isStatePlotVar (const std::string& name)
{
    std::list<std::string>::const_iterator li = state_plot_vars.begin(), end = state_plot_vars.end();

    for ( ; li != end; ++li)
        if (*li == name)
            return true;

    return false;
}

void
Amr::fillStatePlotVarList ()
{
    state_plot_vars.clear();
    const DescriptorList& desc_lst = AmrLevel::get_desc_lst();
    for (int typ = 0; typ < desc_lst.size(); typ++)
        for (int comp = 0; comp < desc_lst[typ].nComp();comp++)
            if (desc_lst[typ].getType() == IndexType::TheCellType())
                state_plot_vars.push_back(desc_lst[typ].name(comp));
}

void
Amr::clearStatePlotVarList ()
{
    state_plot_vars.clear();
}

void
Amr::addStatePlotVar (const std::string& name)
{
    if (!isStatePlotVar(name))
        state_plot_vars.push_back(name);
}

void
Amr::deleteStatePlotVar (const std::string& name)
{
    if (isStatePlotVar(name))
        state_plot_vars.remove(name);
}

bool
Amr::isDerivePlotVar (const std::string& name)
{
    for (std::list<std::string>::const_iterator li = derive_plot_vars.begin(), end = derive_plot_vars.end();
         li != end;
         ++li)
    {
        if (*li == name)
            return true;
    }

    return false;
}

void 
Amr::fillDerivePlotVarList ()
{
    derive_plot_vars.clear();
    DeriveList& derive_lst = AmrLevel::get_derive_lst();
    std::list<DeriveRec>& dlist = derive_lst.dlist();
    for (std::list<DeriveRec>::const_iterator it = dlist.begin(), end = dlist.end();
         it != end;
         ++it)
    {
        if (it->deriveType() == IndexType::TheCellType())
        {
            derive_plot_vars.push_back(it->name());
        }
    }
}

void
Amr::clearDerivePlotVarList ()
{
    derive_plot_vars.clear();
}

void
Amr::addDerivePlotVar (const std::string& name)
{
    if (!isDerivePlotVar(name))
        derive_plot_vars.push_back(name);
}

void
Amr::deleteDerivePlotVar (const std::string& name)
{
    if (isDerivePlotVar(name))
        derive_plot_vars.remove(name);
}

Amr::~Amr ()
{
    if (level_steps[0] > last_checkpoint)
        checkPoint();

    if (level_steps[0] > last_plotfile)
        writePlotFile(plot_file_root,level_steps[0]);

    levelbld->variableCleanUp();
}

void
Amr::setRecordGridInfo (const std::string& filename)
{
    record_grid_info = true;
    if (ParallelDescriptor::IOProcessor())
    {
        gridlog.open(filename.c_str(),std::ios::out|std::ios::app);
        if (!gridlog.good())
            BoxLib::FileOpenFailed(filename);
    }
    ParallelDescriptor::Barrier();
}

void
Amr::setRecordRunInfo (const std::string& filename)
{
    record_run_info = true;
    if (ParallelDescriptor::IOProcessor())
    {
        runlog.open(filename.c_str(),std::ios::out|std::ios::app);
        if (!runlog.good())
            BoxLib::FileOpenFailed(filename);
    }
    ParallelDescriptor::Barrier();
}

void
Amr::setRecordRunInfoTerse (const std::string& filename)
{
    record_run_info_terse = true;
    if (ParallelDescriptor::IOProcessor())
    {
        runlog_terse.open(filename.c_str(),std::ios::out|std::ios::app);
        if (!runlog_terse.good())
            BoxLib::FileOpenFailed(filename);
    }
    ParallelDescriptor::Barrier();
}

void
Amr::setRecordDataInfo (int i, const std::string& filename)
{
    if (ParallelDescriptor::IOProcessor())
    {
        datalog.set(i,new std::ofstream);
        datalog[i].open(filename.c_str(),std::ios::out|std::ios::app);
        if (!datalog[i].good())
            BoxLib::FileOpenFailed(filename);
    }
    ParallelDescriptor::Barrier();
}

void
Amr::setDtLevel (const Array<Real>& dt_lev)
{
    for (int i = 0; i <= finest_level; i++)
        dt_level[i] = dt_lev[i];
}

void
Amr::setNCycle (const Array<int>& ns)
{
    for (int i = 0; i <= finest_level; i++)
        n_cycle[i] = ns[i];
}

long
Amr::cellCount ()
{
    long cnt = 0;
    for (int i = 0; i <= finest_level; i++)
        cnt += amr_level[i].countCells();
    return cnt;
}

int
Amr::numGrids ()
{
    int cnt = 0;
    for (int i = 0; i <= finest_level; i++)
        cnt += amr_level[i].numGrids();
    return cnt;
}

int
Amr::okToContinue ()
{
    int ok = true;
    for (int i = 0; ok && (i <= finest_level); i++)
        ok = ok && amr_level[i].okToContinue();
    return ok;
}

void
Amr::writePlotFile (const std::string& root,
                    int                num)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::writePlotFile()");

    if (!Plot_Files_Output()) return;

    VisMF::SetNOutFiles(plot_nfiles);

    if (first_plotfile) 
    {
        first_plotfile = false;
        amr_level[0].setPlotVariables();
    }

    Real dPlotFileTime0 = ParallelDescriptor::second();

    const std::string pltfile = BoxLib::Concatenate(root,num,file_name_digits);

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
        std::cout << "PLOTFILE: file = " << pltfile << std::endl;

    if (record_run_info && ParallelDescriptor::IOProcessor())
        runlog << "PLOTFILE: file = " << pltfile << '\n';
    //
    // Only the I/O processor makes the directory if it doesn't already exist.
    //
    if (ParallelDescriptor::IOProcessor())
        if (!BoxLib::UtilCreateDirectory(pltfile, 0755))
            BoxLib::CreateDirectoryFailed(pltfile);
    //
    // Force other processors to wait till directory is built.
    //
    ParallelDescriptor::Barrier();

    std::string HeaderFileName = pltfile + "/Header";

    VisMF::IO_Buffer io_buffer(VisMF::IO_Buffer_Size);

    std::ofstream HeaderFile;

    HeaderFile.rdbuf()->pubsetbuf(io_buffer.dataPtr(), io_buffer.size());

    int old_prec = 0;

    if (ParallelDescriptor::IOProcessor())
    {
        //
        // Only the IOProcessor() writes to the header file.
        //
        HeaderFile.open(HeaderFileName.c_str(), std::ios::out|std::ios::trunc|std::ios::binary);
        if (!HeaderFile.good())
            BoxLib::FileOpenFailed(HeaderFileName);
        old_prec = HeaderFile.precision(15);
    }

    for (int k = 0; k <= finest_level; k++)
        amr_level[k].writePlotFile(pltfile, HeaderFile);

    if (ParallelDescriptor::IOProcessor())
    {
        HeaderFile.precision(old_prec);
        if (!HeaderFile.good())
            BoxLib::Error("Amr::writePlotFile() failed");
    }

    const int IOProc = ParallelDescriptor::IOProcessorNumber();

    Real dPlotFileTime1 = ParallelDescriptor::second();
    Real dPlotFileTime  = dPlotFileTime1 - dPlotFileTime0;
    Real wctime         = ParallelDescriptor::second() - probStartTime;

    ParallelDescriptor::ReduceRealMax(wctime,IOProc);
    ParallelDescriptor::ReduceRealMax(dPlotFileTime,IOProc);

    if (ParallelDescriptor::IOProcessor())
    {
        std::cout << "Write plotfile time = "
                  << dPlotFileTime
                  << "  seconds" << '\n'
                  << "Total wall clock seconds since start(restart) = "
                  << wctime << std::endl;
    }
    ParallelDescriptor::Barrier();
}

void
Amr::checkInput ()
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::checkInput()");

    if (max_level < 0)
        BoxLib::Error("checkInput: max_level not set");
    //
    // Check that blocking_factor is a power of 2.
    //
    for (int i = 0; i < max_level; i++)
    {
        int k = blocking_factor[i];
        while ( k > 0 && (k%2 == 0) )
            k /= 2;
        if (k != 1)
            BoxLib::Error("Amr::checkInputs: blocking_factor not power of 2");
    }
    //
    // Check level dependent values.
    //
    int i;
    for (i = 0; i < max_level; i++)
    {
        if (MaxRefRatio(i) < 2 || MaxRefRatio(i) > 12)
            BoxLib::Error("checkInput bad ref_ratios");
    }
    const Box& domain = geom[0].Domain();
    if (!domain.ok())
        BoxLib::Error("level 0 domain bad or not set");
    //
    // Check that domain size is a multiple of blocking_factor[0].
    //
    for (i = 0; i < BL_SPACEDIM; i++)
    {
        int len = domain.length(i);
        if (len%blocking_factor[0] != 0)
            BoxLib::Error("domain size not divisible by blocking_factor");
    }
    //
    // Check that max_grid_size is even.
    //
    for (i = 0; i < max_level; i++)
    {
        if (max_grid_size[i]%2 != 0)
            BoxLib::Error("max_grid_size is not even");
    }

    //
    // Check that max_grid_size is a multiple of blocking_factor at every level.
    //
    for (i = 0; i < max_level; i++)
    {
        if (max_grid_size[i]%blocking_factor[i] != 0)
            BoxLib::Error("max_grid_size not divisible by blocking_factor");
    }

    if (!Geometry::ProbDomain().ok())
        BoxLib::Error("checkInput: bad physical problem size");

    if (max_level > 0) 
       if (regrid_int[0] <= 0)
          BoxLib::Error("checkinput: regrid_int not defined and max_level > 0");

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
       std::cout << "Successfully read inputs file ... " << std::endl;
}

void
Amr::init (Real strt_time,
           Real stop_time)
{
    probStartTime = ParallelDescriptor::second();
    if (!restart_file.empty() && restart_file != "init")
    {
        restart(restart_file);
    }
    else
    {
        initialInit(strt_time,stop_time);
        checkPoint();
        if (plot_int > 0 || plot_per > 0)
            writePlotFile(plot_file_root,level_steps[0]);
    }
#ifdef HAS_XGRAPH
    if (first_plotfile)
    {
        first_plotfile = false;
        amr_level[0].setPlotVariables();
    }
#endif
}

void
Amr::initialInit (Real strt_time,
                  Real stop_time)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::initialInit()");
    checkInput();
    //
    // Generate internal values from user-supplied values.
    //
    finest_level = 0;
    //
    // Init problem dependent data.
    //
    int  init = true;
    //
    // Populate integer array with name of `probin' file.
    //
    int probin_file_length = probin_file.length();

    Array<int> probin_file_name(probin_file_length);

    for (int i = 0; i < probin_file_length; i++)
        probin_file_name[i] = probin_file[i];

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
       std::cout << "Starting to read probin ... " << std::endl;

#if (defined(BL_USE_MPI) && ! defined(BL_USEOLDREADS))
    int nAtOnce(probinit_natonce), myProc(ParallelDescriptor::MyProc());
    int nProcs(ParallelDescriptor::NProcs());
    int nSets((nProcs + (nAtOnce - 1)) / nAtOnce);
    int mySet(myProc/nAtOnce);
    Real piStart, piEnd, piTotal;
    Real piStartAll, piEndAll, piTotalAll;
    piStartAll = ParallelDescriptor::second();
    for(int iSet(0); iSet < nSets; ++iSet) {
      if(mySet == iSet) {  // call the pesky probin reader
        piStart = ParallelDescriptor::second();
        FORT_PROBINIT(&init,
                      probin_file_name.dataPtr(),
                      &probin_file_length,
                      Geometry::ProbLo(),
                      Geometry::ProbHi());
        piEnd = ParallelDescriptor::second();
	const int iBuff(0), wakeUpPID(myProc + nAtOnce), tag(myProc % nAtOnce);
	if(wakeUpPID < nProcs) {
          ParallelDescriptor::Send(&iBuff, 1, wakeUpPID, tag);
	}
      }
      if(mySet == (iSet + 1)) {  // next set waits
	int iBuff, waitForPID(myProc - nAtOnce), tag(myProc % nAtOnce);
        ParallelDescriptor::Recv(&iBuff, 1, waitForPID, tag);
      }
    }  // end for(iSet...)
    piEndAll = ParallelDescriptor::second();
    piTotal = piEnd - piStart;
    piTotalAll = piEndAll - piStartAll;
    ParallelDescriptor::ReduceRealMax(piTotal);
    ParallelDescriptor::ReduceRealMax(piTotalAll);
    if(ParallelDescriptor::IOProcessor()) {
      std::cout << "MFRead:::  PROBINIT max time   = " << piTotal << std::endl;
      std::cout << "MFRead:::  PROBINIT total time = " << piTotalAll << std::endl;
    }
#else
    FORT_PROBINIT(&init,
                  probin_file_name.dataPtr(),
                  &probin_file_length,
                  Geometry::ProbLo(),
                  Geometry::ProbHi());
#endif

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
       std::cout << "Successfully read probin ... " << std::endl;

#ifdef BL_SYNC_RANTABLES
    int iGet(0), iSet(1);
    const int iTableSize(64);
    Real *RanAmpl = new Real[iTableSize];
    Real *RanPhase = new Real[iTableSize];
    FORT_SYNC_RANTABLES(RanPhase, RanAmpl, &iGet);
    ParallelDescriptor::Bcast(RanPhase, iTableSize);
    ParallelDescriptor::Bcast(RanAmpl, iTableSize);
    FORT_SYNC_RANTABLES(RanPhase, RanAmpl, &iSet);
    delete [] RanAmpl;
    delete [] RanPhase;
#endif

    cumtime = strt_time;
    //
    // Define base level grids.
    //
    defBaseLevel(strt_time);
    //
    // Compute dt and set time levels of all grid data.
    //
    amr_level[0].computeInitialDt(finest_level,
                                  sub_cycle,
                                  n_cycle,
                                  ref_ratio,
                                  dt_level,
                                  stop_time);
    //
    // The following was added for multifluid.
    //
    Real dt0   = dt_level[0];
    dt_min[0]  = dt_level[0];
    n_cycle[0] = 1;

    for (int lev = 1; lev <= max_level; lev++)
    {
        const int fact = sub_cycle ? ref_ratio[lev-1][0] : 1;

        dt0           /= Real(fact);
        dt_level[lev]  = dt0;
        dt_min[lev]    = dt_level[lev];
        n_cycle[lev]   = fact;
    }

    if (max_level > 0)
        bldFineLevels(strt_time);

    for (int lev = 0; lev <= finest_level; lev++)
        amr_level[lev].setTimeLevel(strt_time,dt_level[lev],dt_level[lev]);

    for (int lev = 0; lev <= finest_level; lev++)
        amr_level[lev].post_regrid(0,finest_level);
    //
    // Perform any special post_initialization operations.
    //
    for (int lev = 0; lev <= finest_level; lev++)
        amr_level[lev].post_init(stop_time);

    for (int lev = 0; lev <= finest_level; lev++)
    {
        level_count[lev] = 0;
        level_steps[lev] = 0;
    }

    if (ParallelDescriptor::IOProcessor())
       if (verbose > 1) {
           std::cout << "INITIAL GRIDS \n";
           printGridInfo(std::cout,0,finest_level);
       } else if (verbose > 0) { 
           std::cout << "INITIAL GRIDS \n";
           printGridSummary(std::cout,0,finest_level);
       }

    if (record_grid_info && ParallelDescriptor::IOProcessor())
    {
        gridlog << "INITIAL GRIDS \n";
        printGridInfo(gridlog,0,finest_level);
    }

#ifdef USE_STATIONDATA
    station.init();
    station.findGrid(amr_level,geom);
#endif
}

//
// Shared by restart() and checkPoint().
//
static std::string the_previous_ckfile;

void
Amr::restart (const std::string& filename)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::restart()");

    Real dRestartTime0 = ParallelDescriptor::second();

    VisMF::SetMFFileInStreams(mffile_nstreams);

    int i;

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
        std::cout << "restarting calculation from file: " << filename << std::endl;

    if (record_run_info && ParallelDescriptor::IOProcessor())
        runlog << "RESTART from file = " << filename << '\n';
    //
    // Init problem dependent data.
    //
    int init = false;
    //
    // Populate integer array with name of `probin' file.
    //
    int probin_file_length = probin_file.length();

    Array<int> probin_file_name(probin_file_length);

    for (int i = 0; i < probin_file_length; i++)
        probin_file_name[i] = probin_file[i];

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
       std::cout << "Starting to read probin ... " << std::endl;

#if (defined(BL_USE_MPI) && ! defined(BL_USEOLDREADS))
    int nAtOnce(probinit_natonce), myProc(ParallelDescriptor::MyProc());
    int nProcs(ParallelDescriptor::NProcs());
    int nSets((nProcs + (nAtOnce - 1)) / nAtOnce);
    int mySet(myProc/nAtOnce);
    Real piStart, piEnd, piTotal;
    Real piStartAll, piEndAll, piTotalAll;
    piStartAll = ParallelDescriptor::second();
    for(int iSet(0); iSet < nSets; ++iSet) {
      if(mySet == iSet) {  // call the pesky probin reader
        piStart = ParallelDescriptor::second();
        FORT_PROBINIT(&init,
                      probin_file_name.dataPtr(),
                      &probin_file_length,
                      Geometry::ProbLo(),
                      Geometry::ProbHi());
        piEnd = ParallelDescriptor::second();
	const int iBuff(0), wakeUpPID(myProc + nAtOnce), tag(myProc % nAtOnce);
	if(wakeUpPID < nProcs) {
          ParallelDescriptor::Send(&iBuff, 1, wakeUpPID, tag);
	}
      }
      if(mySet == (iSet + 1)) {  // next set waits
	int iBuff, waitForPID(myProc - nAtOnce), tag(myProc % nAtOnce);
        ParallelDescriptor::Recv(&iBuff, 1, waitForPID, tag);
      }
    }  // end for(iSet...)
    piEndAll = ParallelDescriptor::second();
    piTotal = piEnd - piStart;
    piTotalAll = piEndAll - piStartAll;
    ParallelDescriptor::ReduceRealMax(piTotal);
    ParallelDescriptor::ReduceRealMax(piTotalAll);
    if(ParallelDescriptor::IOProcessor()) {
      std::cout << "MFRead:::  PROBINIT max time   = " << piTotal << std::endl;
      std::cout << "MFRead:::  PROBINIT total time = " << piTotalAll << std::endl;
    }
#else
    FORT_PROBINIT(&init,
                  probin_file_name.dataPtr(),
                  &probin_file_length,
                  Geometry::ProbLo(),
                  Geometry::ProbHi());
#endif

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
       std::cout << "Successfully read probin ... " << std::endl;

    //
    // Start calculation from given restart file.
    //
    if (record_run_info && ParallelDescriptor::IOProcessor())
        runlog << "RESTART from file = " << filename << '\n';
    //
    // Open the checkpoint header file for reading.
    //
    std::string File = filename;

    File += '/';
    File += "Header";

    VisMF::IO_Buffer io_buffer(VisMF::IO_Buffer_Size);

#if (defined(BL_USE_MPI) && ! defined(BL_USEOLDREADS))
    Array<char> fileCharPtr;
    ParallelDescriptor::ReadAndBcastFile(File, fileCharPtr);
    std::istrstream is(fileCharPtr.dataPtr());

    is.rdbuf()->pubsetbuf(io_buffer.dataPtr(), io_buffer.size());  // hmmm?
#else
    // define BL_USEOLDREADS to use the old way.
    std::ifstream is;

    is.rdbuf()->pubsetbuf(io_buffer.dataPtr(), io_buffer.size());

    is.open(File.c_str(), std::ios::in);

    if (!is.good())
        BoxLib::FileOpenFailed(File);
#endif

    //
    // Read global data.
    //
    // Attempt to differentiate between old and new CheckPointFiles.
    //
    int         spdim;
    bool        new_checkpoint_format = false;
    std::string first_line;

    std::getline(is,first_line);

    if (first_line == CheckPointVersion)
    {
        new_checkpoint_format = true;
        is >> spdim;
    }
    else
    {
        spdim = atoi(first_line.c_str());
    }

    if (spdim != BL_SPACEDIM)
    {
        std::cerr << "Amr::restart(): bad spacedim = " << spdim << '\n';
        BoxLib::Abort();
    }

    is >> cumtime;
    int mx_lev;
    is >> mx_lev;
    is >> finest_level;

    if (max_level >= mx_lev) {

       for (i = 0; i <= mx_lev; i++) is >> geom[i];
       for (i = 0; i <  mx_lev; i++) is >> ref_ratio[i];
       for (i = 0; i <= mx_lev; i++) is >> dt_level[i];

       if (new_checkpoint_format)
       {
           for (i = 0; i <= mx_lev; i++) is >> dt_min[i];
       }
       else
       {
           for (i = 0; i <= mx_lev; i++) dt_min[i] = dt_level[i];
       }

       for (i = 0; i <= mx_lev; i++) is >> n_cycle[i];
       for (i = 0; i <= mx_lev; i++) is >> level_steps[i];
       for (i = 0; i <= mx_lev; i++) is >> level_count[i];

       //
       // Set bndry conditions.
       //
       if (max_level > mx_lev)
       {
           for (i = mx_lev+1; i <= max_level; i++)
           {
               const int rat  = MaxRefRatio(i-1);
               const int mult = sub_cycle ? rat : 1;
   
               dt_level[i]    = dt_level[i-1]/Real(rat);
               n_cycle[i]     = mult;
               level_steps[i] = mult*level_steps[i-1];
               level_count[i] = 0;
           }
           if (!sub_cycle)
           {
               for (i = 0; i <= max_level; i++)
                   dt_level[i] = dt_level[max_level];
           }
       }

       if (regrid_on_restart and max_level > 0)
           level_count[0] = regrid_int[0];

       checkInput();
       //
       // Read levels.
       //
       int lev;
       for (lev = 0; lev <= finest_level; lev++)
       {
           amr_level.set(lev,(*levelbld)());
           amr_level[lev].restart(*this, is);
       }
       //
       // Build any additional data structures.
       //
       for (lev = 0; lev <= finest_level; lev++)
           amr_level[lev].post_restart();

    } else {

       if (ParallelDescriptor::IOProcessor())
          BoxLib::Warning("Amr::restart(): max_level is lower than before");

       int new_finest_level = std::min(max_level,finest_level);

       finest_level = new_finest_level;
 
       // These are just used to hold the extra stuff we have to read in.
       Geometry   geom_dummy;
       Real       real_dummy;
       int         int_dummy;
       IntVect intvect_dummy;

       for (i = 0          ; i <= max_level; i++) is >> geom[i];
       for (i = max_level+1; i <= mx_lev   ; i++) is >> geom_dummy;

       for (i = 0        ; i <  max_level; i++) is >> ref_ratio[i];
       for (i = max_level; i <  mx_lev   ; i++) is >> intvect_dummy;

       for (i = 0          ; i <= max_level; i++) is >> dt_level[i];
       for (i = max_level+1; i <= mx_lev   ; i++) is >> real_dummy;

       if (new_checkpoint_format)
       {
           for (i = 0          ; i <= max_level; i++) is >> dt_min[i];
           for (i = max_level+1; i <= mx_lev   ; i++) is >> real_dummy;
       }
       else
       {
           for (i = 0; i <= max_level; i++) dt_min[i] = dt_level[i];
       }

       for (i = 0          ; i <= max_level; i++) is >> n_cycle[i];
       for (i = max_level+1; i <= mx_lev   ; i++) is >> int_dummy;

       for (i = 0          ; i <= max_level; i++) is >> level_steps[i];
       for (i = max_level+1; i <= mx_lev   ; i++) is >> int_dummy;

       for (i = 0          ; i <= max_level; i++) is >> level_count[i];
       for (i = max_level+1; i <= mx_lev   ; i++) is >> int_dummy;

       if (regrid_on_restart and max_level > 0)
           level_count[0] = regrid_int[0];

       checkInput();

       //
       // Read levels.
       //
       int lev;
       for (lev = 0; lev <= new_finest_level; lev++)
       {
           amr_level.set(lev,(*levelbld)());
           amr_level[lev].restart(*this, is);
       }
       //
       // Build any additional data structures.
       //
       for (lev = 0; lev <= new_finest_level; lev++)
           amr_level[lev].post_restart();

    }
   
#ifdef USE_STATIONDATA
    station.init();
    station.findGrid(amr_level,geom);
#endif

    const int IOProc = ParallelDescriptor::IOProcessorNumber();

    Real dRestartTime1 = ParallelDescriptor::second();
    Real dRestartTime  = dRestartTime1 - dRestartTime0;

    ParallelDescriptor::ReduceRealMax(dRestartTime,IOProc);
    if (ParallelDescriptor::IOProcessor())
    {
        std::cout << "Restart time = " << dRestartTime << " seconds." << std::endl;

        the_previous_ckfile = filename;
    }
}

void
Amr::checkPoint ()
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::checkPoint()");

    if (!checkpoint_files_output) return;

    VisMF::SetNOutFiles(checkpoint_nfiles);
    //
    // In checkpoint files always write out FABs in NATIVE format.
    //
    FABio::Format thePrevFormat = FArrayBox::getFormat();

    FArrayBox::setFormat(FABio::FAB_NATIVE);

    Real dCheckPointTime0 = ParallelDescriptor::second();

    const std::string ckfile = BoxLib::Concatenate(check_file_root,level_steps[0],file_name_digits);

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
        std::cout << "CHECKPOINT: file = " << ckfile << std::endl;

    if (record_run_info && ParallelDescriptor::IOProcessor())
        runlog << "CHECKPOINT: file = " << ckfile << '\n';
    //
    // Only the I/O processor makes the directory if it doesn't already exist.
    //
    if (ParallelDescriptor::IOProcessor())
        if (!BoxLib::UtilCreateDirectory(ckfile, 0755))
            BoxLib::CreateDirectoryFailed(ckfile);
    //
    // Force other processors to wait till directory is built.
    //
    ParallelDescriptor::Barrier();

    std::string HeaderFileName = ckfile + "/Header";

    VisMF::IO_Buffer io_buffer(VisMF::IO_Buffer_Size);

    std::ofstream HeaderFile;

    HeaderFile.rdbuf()->pubsetbuf(io_buffer.dataPtr(), io_buffer.size());

    int old_prec = 0, i;

    if (ParallelDescriptor::IOProcessor())
    {
        //
        // Only the IOProcessor() writes to the header file.
        //
        HeaderFile.open(HeaderFileName.c_str(), std::ios::out|std::ios::trunc|std::ios::binary);

        if (!HeaderFile.good())
            BoxLib::FileOpenFailed(HeaderFileName);

        old_prec = HeaderFile.precision(15);

        HeaderFile << CheckPointVersion << '\n'
                   << BL_SPACEDIM       << '\n'
                   << cumtime           << '\n'
                   << max_level         << '\n'
                   << finest_level      << '\n';
        //
        // Write out problem domain.
        //
        for (i = 0; i <= max_level; i++) HeaderFile << geom[i]        << ' ';
        HeaderFile << '\n';
        for (i = 0; i < max_level; i++)  HeaderFile << ref_ratio[i]   << ' ';
        HeaderFile << '\n';
        for (i = 0; i <= max_level; i++) HeaderFile << dt_level[i]    << ' ';
        HeaderFile << '\n';
        for (i = 0; i <= max_level; i++) HeaderFile << dt_min[i]      << ' ';
        HeaderFile << '\n';
        for (i = 0; i <= max_level; i++) HeaderFile << n_cycle[i]     << ' ';
        HeaderFile << '\n';
        for (i = 0; i <= max_level; i++) HeaderFile << level_steps[i] << ' ';
        HeaderFile << '\n';
        for (i = 0; i <= max_level; i++) HeaderFile << level_count[i] << ' ';
        HeaderFile << '\n';
    }

    for (i = 0; i <= finest_level; i++)
        amr_level[i].checkPoint(ckfile, HeaderFile);

    if (ParallelDescriptor::IOProcessor())
    {
        HeaderFile.precision(old_prec);

        if (!HeaderFile.good())
            BoxLib::Error("Amr::checkpoint() failed");
    }

#ifdef USE_SLABSTAT
    //
    // Dump out any SlabStats MultiFabs.
    //
    AmrLevel::get_slabstat_lst().checkPoint(getAmrLevels(), level_steps[0]);
#endif

    //
    // Don't forget to reset FAB format.
    //
    FArrayBox::setFormat(thePrevFormat);

    const int IOProc     = ParallelDescriptor::IOProcessorNumber();
    Real dCheckPointTime = ParallelDescriptor::second() - dCheckPointTime0;

    ParallelDescriptor::ReduceRealMax(dCheckPointTime,IOProc);

    if (ParallelDescriptor::IOProcessor())
        std::cout << "checkPoint() time = "
                  << dCheckPointTime
                  << " secs." << std::endl;
    ParallelDescriptor::Barrier();
}

void
Amr::RegridOnly (Real time)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::RegridOnly()");

    BL_ASSERT(regrid_on_restart == 1);

    int lev_top = std::min(finest_level, max_level-1);

    for (int i = 0; i <= lev_top; i++)
       regrid(i,time);

    if (plotfile_on_restart)
	writePlotFile(plot_file_root,level_steps[0]);

    if (checkpoint_on_restart)
       checkPoint();

}

void
Amr::timeStep (int  level,
               Real time,
               int  iteration,
               int  niter,
               Real stop_time)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::timeStep()");
    //
    // Allow regridding of level 0 calculation on restart.
    //
    if (finest_level == 0 && regrid_on_restart)
    {
        regrid_on_restart = 0;
        //
        // Coarsening before we split the grids ensures that each resulting
        // grid will have an even number of cells in each direction.
        //
        BoxArray lev0(BoxLib::coarsen(geom[0].Domain(),2));
        //
        // Now split up into list of grids within max_grid_size[0] limit.
        //
        lev0.maxSize(max_grid_size[0]/2);
        //
        // Now refine these boxes back to level 0.
        //
        lev0.refine(2);
        //
        // Construct skeleton of new level.
        //
        AmrLevel* a = (*levelbld)(*this,0,geom[0],lev0,cumtime);

        a->init(amr_level[0]);
        amr_level.clear(0);
        amr_level.set(0,a);

        if (ParallelDescriptor::IOProcessor())
           if (verbose > 1) {
              printGridInfo(std::cout,0,finest_level);
           } else if (verbose > 0) {
              printGridSummary(std::cout,0,finest_level);
           }

        if (record_grid_info && ParallelDescriptor::IOProcessor())
            printGridInfo(gridlog,0,finest_level);
    }
    else
    {
        int lev_top = std::min(finest_level, max_level-1);

        for (int i = level; i <= lev_top; i++)
        {
            const int old_finest = finest_level;

            if (level_count[i] >= regrid_int[i] && amr_level[i].okToRegrid())
            {
                regrid(i,time);

                //
                // Compute new dt after regrid if at level 0 and compute_new_dt_on_regrid.
                //
                if ( compute_new_dt_on_regrid && (i == 0) )
                {
                    int post_regrid_flag = 1;
                    amr_level[0].computeNewDt(finest_level,
                                              sub_cycle,
                                              n_cycle,
                                              ref_ratio,
                                              dt_min,
                                              dt_level,
                                              stop_time, 
                                              post_regrid_flag);
                }

                for (int k = i; k <= finest_level; k++)
                    level_count[k] = 0;

                if (old_finest < finest_level)
                {
                    //
                    // The new levels will not have valid time steps
                    // and iteration counts.
                    //
                    for (int k = old_finest+1; k <= finest_level; k++)
                    {
                        const int fact = sub_cycle ? MaxRefRatio(k-1) : 1;
                        dt_level[k]    = dt_level[k-1]/Real(fact);
                        n_cycle[k]     = fact;
                    }
                }
            }
            if (old_finest > finest_level)
                lev_top = std::min(finest_level, max_level-1);
        }
    }
    //
    // Check to see if should write plotfile.
    // This routine is here so it is done after the restart regrid.
    //
    if (plotfile_on_restart && !(restart_file.empty()) )
    {
	plotfile_on_restart = 0;
	writePlotFile(plot_file_root,level_steps[0]);
    }
    //
    // Advance grids at this level.
    //
    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
    {
        std::cout << "ADVANCE grids at level "
                  << level
                  << " with dt = "
                  << dt_level[level]
                  << std::endl;
    }
    Real dt_new = amr_level[level].advance(time,dt_level[level],iteration,niter);

    dt_min[level] = iteration == 1 ? dt_new : std::min(dt_min[level],dt_new);

    level_steps[level]++;
    level_count[level]++;

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
    {
        std::cout << "Advanced "
                  << amr_level[level].countCells()
                  << " cells at level "
                  << level
                  << std::endl;
    }

#ifdef USE_STATIONDATA
    station.report(time+dt_level[level],level,amr_level[level]);
#endif

#ifdef USE_SLABSTAT
    AmrLevel::get_slabstat_lst().update(amr_level[level],time,dt_level[level]);
#endif
    //
    // Advance grids at higher level.
    //
    if (level < finest_level)
    {
        const int lev_fine = level+1;

        if (sub_cycle)
        {
            const int ncycle = n_cycle[lev_fine];

            for (int i = 1; i <= ncycle; i++)
                timeStep(lev_fine,time+(i-1)*dt_level[lev_fine],i,ncycle,stop_time);
        }
        else
        {
            timeStep(lev_fine,time,1,1,stop_time);
        }
    }

    amr_level[level].post_timestep(iteration);
}

void
Amr::coarseTimeStep (Real stop_time)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::coarseTimeStep()");

    const Real run_strt = ParallelDescriptor::second() ;
    //
    // Compute new dt.
    //
    if (level_steps[0] > 0)
    {
        int post_regrid_flag = 0;
        amr_level[0].computeNewDt(finest_level,
                                  sub_cycle,
                                  n_cycle,
                                  ref_ratio,
                                  dt_min,
                                  dt_level,
                                  stop_time,
                                  post_regrid_flag);
    }
    timeStep(0,cumtime,1,1,stop_time);

    cumtime += dt_level[0];

    amr_level[0].postCoarseTimeStep(cumtime);

    if (verbose > 0)
    {
        Real run_stop = ParallelDescriptor::second() - run_strt;

        ParallelDescriptor::ReduceRealMax(run_stop,ParallelDescriptor::IOProcessorNumber());

        if (ParallelDescriptor::IOProcessor())
            std::cout << "\nCoarse TimeStep time: " << run_stop << '\n' ;

        long min_fab_bytes = BoxLib::total_bytes_allocated_in_fabs_hwm;
        long max_fab_bytes = BoxLib::total_bytes_allocated_in_fabs_hwm;

        ParallelDescriptor::ReduceLongMin(min_fab_bytes,ParallelDescriptor::IOProcessorNumber());
        ParallelDescriptor::ReduceLongMax(max_fab_bytes,ParallelDescriptor::IOProcessorNumber());
        //
        // Reset to zero to calculate high-water-mark for next timestep.
        //
        BoxLib::total_bytes_allocated_in_fabs_hwm = 0;

        if (ParallelDescriptor::IOProcessor())
            std::cout << "\nFAB byte spread across MPI nodes for timestep: ["
                      << min_fab_bytes << " ... " << max_fab_bytes << "]\n";
    }

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
    {
        std::cout << "\nSTEP = "
                 << level_steps[0]
                  << " TIME = "
                  << cumtime
                  << " DT = "
                  << dt_level[0]
                  << '\n'
                  << std::endl;
    }
    if (record_run_info && ParallelDescriptor::IOProcessor())
    {
        runlog << "STEP = "
               << level_steps[0]
               << " TIME = "
               << cumtime
               << " DT = "
               << dt_level[0]
               << '\n';
    }
    if (record_run_info_terse && ParallelDescriptor::IOProcessor())
        runlog_terse << level_steps[0] << " " << cumtime << " " << dt_level[0] << '\n';

    int check_test = 0;
    if (check_per > 0.0)
    {
      const int num_per_old = cumtime / check_per;
      const int num_per_new = (cumtime+dt_level[0]) / check_per;

      if (num_per_old != num_per_new)
	{
	 check_test = 1;
	}
    }

    if ((check_int > 0 && level_steps[0] % check_int == 0) || check_test == 1)
    {
        last_checkpoint = level_steps[0];
        checkPoint();
    }

    int plot_test = 0;
    if (plot_per > 0.0)
    {
      const int num_per_old = (cumtime-dt_level[0]) / plot_per;
      const int num_per_new = (cumtime            ) / plot_per;

      if (num_per_old != num_per_new)
	{
	  plot_test = 1;
	}
    }

    if ((plot_int > 0 && level_steps[0] % plot_int == 0) || plot_test == 1)
    {
        last_plotfile = level_steps[0];
        writePlotFile(plot_file_root,level_steps[0]);
    }
}

void
Amr::defBaseLevel (Real strt_time)
{
    //
    // Check that base domain has even number of zones in all directions.
    //
    const Box& domain = geom[0].Domain();
    IntVect d_length  = domain.size();
    const int* d_len  = d_length.getVect();

    for (int idir = 0; idir < BL_SPACEDIM; idir++)
        if (d_len[idir]%2 != 0)
            BoxLib::Error("defBaseLevel: must have even number of cells");
    //
    // Coarsening before we split the grids ensures that each resulting
    // grid will have an even number of cells in each direction.
    //
    BoxArray lev0(1);

    lev0.set(0,BoxLib::coarsen(domain,2));
    //
    // Now split up into list of grids within max_grid_size[0] limit.
    //
    lev0.maxSize(max_grid_size[0]/2);
    //
    // Now refine these boxes back to level 0.
    //
    lev0.refine(2);
    //
    // Now build level 0 grids.
    //
    amr_level.set(0,(*levelbld)(*this,0,geom[0],lev0,strt_time));

    lev0.clear();
    //
    // Now init level 0 grids with data.
    //
    amr_level[0].initData();
}

void
Amr::regrid (int  lbase,
             Real time,
             bool initial)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::regrid()");

    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
        std::cout << "REGRID: at level lbase = " << lbase << std::endl;

    if (record_run_info && ParallelDescriptor::IOProcessor())
        runlog << "REGRID: at level lbase = " << lbase << std::endl;
    //
    // Remove old-time grid space at highest level.
    //
    if (finest_level == max_level)
        amr_level[finest_level].removeOldData();
    //
    // Compute positions of new grids.
    //
    int             new_finest;
    Array<BoxArray> new_grid_places(max_level+1);

    if (lbase <= std::min(finest_level,max_level-1))
      grid_places(lbase,time,new_finest, new_grid_places);

    bool regrid_level_zero =
        lbase == 0 && new_grid_places[0] != amr_level[0].boxArray();

    const int start = regrid_level_zero ? 0 : lbase+1;
    //
    // Reclaim old-time grid space for all remain levels > lbase.
    //
    for (int lev = start; lev <= finest_level; lev++)
        amr_level[lev].removeOldData();
    //
    // Reclaim all remaining storage for levels > new_finest.
    //
    for (int lev = new_finest+1; lev <= finest_level; lev++)
        amr_level.clear(lev);

    finest_level = new_finest;

    if (lbase == 0)
    {
        FabArrayBase::CPC::FlushCache();
        MultiFab::FlushSICache();
        Geometry::FlushPIRMCache();
        DistributionMapping::FlushCache();

        if (!regrid_level_zero)
        {
            const MultiFab& mf = amr_level[0].get_new_data(0);

            BL_ASSERT(mf.ok());

            DistributionMapping::AddToCache(mf.DistributionMap());
        }

#ifdef USE_SLABSTAT
        //
        // Recache the distribution maps for SlabStat MFs.
        //
        std::list<SlabStatRec*>& ssl = AmrLevel::get_slabstat_lst().list();

        for (std::list<SlabStatRec*>::iterator li = ssl.begin(), end = ssl.end();
             li != end;
             ++li)
        {
            DistributionMapping::AddToCache((*li)->mf().DistributionMap());
        }
#endif
    }
    //
    // Define the new grids from level start up to new_finest.
    //
    for (int lev = start; lev <= new_finest; lev++)
    {
        //
        // Construct skeleton of new level.
        //
        AmrLevel* a = (*levelbld)(*this,lev,geom[lev],new_grid_places[lev],cumtime);

        if (initial)
        {
            //
            // We're being called on startup from bldFineLevels().
            // NOTE: The initData function may use a filPatch, and so needs to
            //       be officially inserted into the hierarchy prior to the call.
            //
            amr_level.clear(lev);
            amr_level.set(lev,a);
            amr_level[lev].initData();
        }
        else if (amr_level.defined(lev))
        {
            //
            // Init with data from old structure then remove old structure.
            // NOTE: The init function may use a filPatch from the old level,
            //       which therefore needs remain in the hierarchy during the call.
            //
            a->init(amr_level[lev]);
            amr_level.clear(lev);
            amr_level.set(lev,a);
       }
        else
        {
            a->init();
            amr_level.clear(lev);
            amr_level.set(lev,a);
        }

    }
    //
    // Build any additional data structures after grid generation.
    //
    for (int lev = 0; lev <= new_finest; lev++)
        amr_level[lev].post_regrid(lbase,new_finest);

#ifdef USE_STATIONDATA
    station.findGrid(amr_level,geom);
#endif
    //
    // Report creation of new grids.
    //
    if (record_run_info && ParallelDescriptor::IOProcessor())
    {
        printGridInfo(runlog,start,finest_level);
    }
    if (record_grid_info && ParallelDescriptor::IOProcessor())
    {
        if (lbase == 0)
            gridlog << "STEP = " << level_steps[0] << ' ';

        gridlog << "TIME = "
                << time
                << " : REGRID  with lbase = "
                << lbase
                << std::endl;

        printGridInfo(gridlog,start,finest_level);
    }
    if ((verbose > 0) && ParallelDescriptor::IOProcessor())
    {
        if (lbase == 0)
            std::cout << "STEP = " << level_steps[0] << ' ';

        std::cout << "TIME = "
                  << time
                  << " : REGRID  with lbase = "
                  << lbase
                  << std::endl;

        if (verbose > 1) {
           printGridInfo(std::cout,start,finest_level);
        } else {
           printGridSummary(std::cout,start,finest_level);
        }
    }
}

void
Amr::printGridInfo (std::ostream& os,
                    int           min_lev,
                    int           max_lev)
{
    for (int lev = min_lev; lev <= max_lev; lev++)
    {
        const BoxArray&           bs      = amr_level[lev].boxArray();
        int                       numgrid = bs.size();
        long                      ncells  = amr_level[lev].countCells();
        double                    ntot    = geom[lev].Domain().d_numPts();
        Real                      frac    = 100.0*(Real(ncells) / ntot);
        const DistributionMapping& map    = amr_level[lev].get_new_data(0).DistributionMap();

        os << "  Level "
           << lev
           << "   "
           << numgrid
           << " grids  "
           << ncells
           << " cells  "
           << frac
           << " % of domain"
           << '\n';


        for (int k = 0; k < numgrid; k++)
        {
            const Box& b = bs[k];

            os << ' ' << lev << ": " << b << "   ";
                
            for (int i = 0; i < BL_SPACEDIM; i++)
                os << b.length(i) << ' ';

            os << ":: " << map[k] << '\n';
        }
    }

    os << std::endl; // Make sure we flush!
}

void
Amr::printGridSummary (std::ostream& os,
                       int           min_lev,
                       int           max_lev)
{
    for (int lev = min_lev; lev <= max_lev; lev++)
    {
        const BoxArray&           bs      = amr_level[lev].boxArray();
        int                       numgrid = bs.size();
        long                      ncells  = amr_level[lev].countCells();
        double                    ntot    = geom[lev].Domain().d_numPts();
        Real                      frac    = 100.0*(Real(ncells) / ntot);
        const DistributionMapping& map    = amr_level[lev].get_new_data(0).DistributionMap();

        os << "  Level "
           << lev
           << "   "
           << numgrid
           << " grids  "
           << ncells
           << " cells  "
           << frac
           << " % of domain"
           << '\n';
    }

    os << std::endl; // Make sure we flush!
}

void
Amr::ProjPeriodic (BoxList&        blout,
                   const Geometry& geom)
{
    //
    // Add periodic translates to blout.
    //
    BL_PROFILE("Amr::ProjPeriodic()");

    Box domain = geom.Domain();

    BoxList blorig(blout);

    int nist,njst,nkst;
    int niend,njend,nkend;
    nist = njst = nkst = 0;
    niend = njend = nkend = 0;
    D_TERM( nist , =njst , =nkst ) = -1;
    D_TERM( niend , =njend , =nkend ) = +1;

    int ri,rj,rk;
    for (ri = nist; ri <= niend; ri++)
    {
        if (ri != 0 && !geom.isPeriodic(0))
            continue;
        if (ri != 0 && geom.isPeriodic(0))
            blorig.shift(0,ri*domain.length(0));
        for (rj = njst; rj <= njend; rj++)
        {
            if (rj != 0 && !geom.isPeriodic(1))
                continue;
            if (rj != 0 && geom.isPeriodic(1))
                blorig.shift(1,rj*domain.length(1));
            for (rk = nkst; rk <= nkend; rk++)
            {
                if (rk != 0 && !geom.isPeriodic(2))
                    continue;
                if (rk != 0 && geom.isPeriodic(2))
                    blorig.shift(2,rk*domain.length(2));

                BoxList tmp(blorig);
                tmp.intersect(domain);
                blout.join(tmp);
 
                if (rk != 0 && geom.isPeriodic(2))
                    blorig.shift(2,-rk*domain.length(2));
            }
            if (rj != 0 && geom.isPeriodic(1))
                blorig.shift(1,-rj*domain.length(1));
        }
        if (ri != 0 && geom.isPeriodic(0))
            blorig.shift(0,-ri*domain.length(0));
    }
}

void
Amr::grid_places (int              lbase,
                  Real             time,
                  int&             new_finest,
                  Array<BoxArray>& new_grids)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::grid_places()");

    int i, max_crse = std::min(finest_level,max_level-1);

    const Real strttime = ParallelDescriptor::second();

    if (lbase == 0)
    {
        //
        // Recalculate level 0 BoxArray in case max_grid_size[0] has changed.
        // This is done exactly as in defBaseLev().
        //
        BoxArray lev0(1);

        lev0.set(0,BoxLib::coarsen(geom[0].Domain(),2));
        //
        // Now split up into list of grids within max_grid_size[0] limit.
        //
        lev0.maxSize(max_grid_size[0]/2);
        //
        // Now refine these boxes back to level 0.
        //
        lev0.refine(2);

        new_grids[0] = lev0;
    }

    if (!grids_file.empty())
    {
#define STRIP while( is.get() != '\n' )

        std::ifstream is(grids_file.c_str(),std::ios::in);

        if (!is.good())
            BoxLib::FileOpenFailed(grids_file);

        new_finest = std::min(max_level,(finest_level+1));
        int in_finest;
        is >> in_finest;
        STRIP;
        new_finest = std::min(new_finest,in_finest);
        int ngrid;
        for (int lev = 1; lev <= new_finest; lev++)
        {
            BoxList bl;
            is >> ngrid;
            STRIP;
            for (i = 0; i < ngrid; i++)
            {
                Box bx;
                is >> bx;
                STRIP;
                if (lev > lbase)
                {
                    bx.refine(ref_ratio[lev-1]);
                    if (bx.longside() > max_grid_size[lev])
                    {
                        std::cout << "Grid " << bx << " too large" << '\n';
                        BoxLib::Error();
                    }
                    bl.push_back(bx);
                }
            }
            if (lev > lbase)
                new_grids[lev].define(bl);
        }
        is.close();
        return;
#undef STRIP
    }
    //
    // Construct problem domain at each level.
    //
    Array<IntVect> bf_lev(max_level); // Blocking factor at each level.
    Array<IntVect> rr_lev(max_level);
    Array<Box>     pc_domain(max_level);  // Coarsened problem domain.
    for (i = 0; i <= max_crse; i++)
    {
        for (int n=0; n<BL_SPACEDIM; n++)
            bf_lev[i][n] = std::max(1,blocking_factor[i]/ref_ratio[i][n]);
    }
    for (i = lbase; i < max_crse; i++)
    {
        for (int n=0; n<BL_SPACEDIM; n++)
            rr_lev[i][n] = (ref_ratio[i][n]*bf_lev[i][n])/bf_lev[i+1][n];
    }
    for (i = lbase; i <= max_crse; i++)
        pc_domain[i] = BoxLib::coarsen(geom[i].Domain(),bf_lev[i]);
    //
    // Construct proper nesting domains.
    //
    Array<BoxList> p_n(max_level);      // Proper nesting domain.
    Array<BoxList> p_n_comp(max_level); // Complement proper nesting domain.

    BoxList bl(amr_level[lbase].boxArray());
    bl.simplify();
    bl.coarsen(bf_lev[lbase]);
    p_n_comp[lbase].complementIn(pc_domain[lbase],bl);
    p_n_comp[lbase].simplify();
    p_n_comp[lbase].accrete(n_proper);
    Amr::ProjPeriodic(p_n_comp[lbase], Geometry(pc_domain[lbase]));
    p_n[lbase].complementIn(pc_domain[lbase],p_n_comp[lbase]);
    p_n[lbase].simplify();
    bl.clear();

    for (i = lbase+1; i <= max_crse; i++)
    {
        p_n_comp[i] = p_n_comp[i-1];
        p_n_comp[i].refine(rr_lev[i-1]);
        p_n_comp[i].accrete(n_proper);
        Amr::ProjPeriodic(p_n_comp[i], Geometry(pc_domain[i]));
        p_n[i].complementIn(pc_domain[i],p_n_comp[i]);
        p_n[i].simplify();
    }
    //
    // Now generate grids from finest level down.
    //
    new_finest = lbase;

    for (int levc = max_crse; levc >= lbase; levc--)
    {
        int levf = levc+1;
        //
        // Construct TagBoxArray with sufficient grow factor to contain
        // new levels projected down to this level.
        //
        int ngrow = 0;

        if (levf < new_finest)
        {
            BoxArray ba_proj(new_grids[levf+1]);

            ba_proj.coarsen(ref_ratio[levf]);
            ba_proj.grow(n_proper);
            ba_proj.coarsen(ref_ratio[levc]);

            BoxArray levcBA = amr_level[levc].boxArray();

            while (!levcBA.contains(ba_proj))
            {
                BoxArray tmp = levcBA;
                tmp.grow(1);
                levcBA = tmp;
                ngrow++;
            }
        }
        TagBoxArray tags(amr_level[levc].boxArray(),n_error_buf[levc]+ngrow);

        amr_level[levc].errorEst(tags,
                                 TagBox::CLEAR,TagBox::SET,time,
                                 n_error_buf[levc],ngrow);
        //
        // If new grids have been constructed above this level, project
        // those grids down and tag cells on intersections to ensure
        // proper nesting.
        //
        // NOTE: this loop replaces the previous code:
        //      if (levf < new_finest) 
        //          tags.setVal(ba_proj,TagBox::SET);
        // The problem with this code is that it effectively 
        // "buffered the buffer cells",  i.e., the grids at level
        // levf+1 which were created by buffering with n_error_buf[levf]
        // are then coarsened down twice to define tagging at
        // level levc, which will then also be buffered.  This can
        // create grids which are larger than necessary.
        //
        if (levf < new_finest)
        {
            int nerr = n_error_buf[levf];

            BoxList bl_tagged(new_grids[levf+1]);
            bl_tagged.simplify();
            bl_tagged.coarsen(ref_ratio[levf]);
            //
            // This grows the boxes by nerr if they touch the edge of the
            // domain in preparation for them being shrunk by nerr later.
            // We want the net effect to be that grids are NOT shrunk away
            // from the edges of the domain.
            //
            for (BoxList::iterator blt = bl_tagged.begin(), end = bl_tagged.end();
                 blt != end;
                 ++blt)
            {
                for (int idir = 0; idir < BL_SPACEDIM; idir++)
                {
                    if (blt->smallEnd(idir) == geom[levf].Domain().smallEnd(idir))
                        blt->growLo(idir,nerr);
                    if (blt->bigEnd(idir) == geom[levf].Domain().bigEnd(idir))
                        blt->growHi(idir,nerr);
                }
            }
            Box mboxF = BoxLib::grow(bl_tagged.minimalBox(),1);
            BoxList blFcomp;
            blFcomp.complementIn(mboxF,bl_tagged);
            blFcomp.simplify();
            bl_tagged.clear();

            const IntVect iv = IntVect(D_DECL(nerr/ref_ratio[levf][0],
                                              nerr/ref_ratio[levf][1],
                                              nerr/ref_ratio[levf][2]));
            blFcomp.accrete(iv);
            BoxList blF;
            blF.complementIn(mboxF,blFcomp);
            BoxArray baF(blF);
            blF.clear();
            baF.grow(n_proper);
            //
            // We need to do this in case the error buffering at
            // levc will not be enough to cover the error buffering
            // at levf which was just subtracted off.
            //
            for (int idir = 0; idir < BL_SPACEDIM; idir++) 
            {
                if (nerr > n_error_buf[levc]*ref_ratio[levc][idir]) 
                    baF.grow(idir,nerr-n_error_buf[levc]*ref_ratio[levc][idir]);
            }

            baF.coarsen(ref_ratio[levc]);

            tags.setVal(baF,TagBox::SET);
        }
        //
        // Buffer error cells.
        //
        tags.buffer(n_error_buf[levc]+ngrow);
        //
        // Coarsen the taglist by blocking_factor/ref_ratio.
        //
        int bl_max = 0;
        for (int n=0; n<BL_SPACEDIM; n++)
            bl_max = std::max(bl_max,bf_lev[levc][n]);
        if (bl_max > 1) 
            tags.coarsen(bf_lev[levc]);
        //
        // Remove or add tagged points which violate/satisfy additional 
        // user-specified criteria.
        //
        amr_level[levc].manual_tags_placement(tags, bf_lev);
        //
        // Map tagged points through periodic boundaries, if any.
        //
        tags.mapPeriodic(Geometry(pc_domain[levc]));
        //
        // Remove cells outside proper nesting domain for this level.
        //
        tags.setVal(p_n_comp[levc],TagBox::CLEAR);
        //
        // Create initial cluster containing all tagged points.
        //
        long     len = 0;
        IntVect* pts = tags.collate(len);

        tags.clear();

        if (len > 0)
        {
            //
            // Created new level, now generate efficient grids.
            //
            new_finest = std::max(new_finest,levf);
            //
            // Construct initial cluster.
            //
            ClusterList clist(pts,len);
            clist.chop(grid_eff);
            BoxDomain bd;
            bd.add(p_n[levc]);
            clist.intersect(bd);
            bd.clear();
            //
            // Efficient properly nested Clusters have been constructed
            // now generate list of grids at level levf.
            //
            BoxList new_bx;
            clist.boxList(new_bx);
            new_bx.refine(bf_lev[levc]);
            new_bx.simplify();
            BL_ASSERT(new_bx.isDisjoint());
            IntVect largest_grid_size;
            for (int n = 0; n < BL_SPACEDIM; n++)
                largest_grid_size[n] = max_grid_size[levf] / ref_ratio[levc][n];
            //
            // Ensure new grid boxes are at most max_grid_size in index dirs.
            //
            new_bx.maxSize(largest_grid_size);

#ifdef BL_FIX_GATHERV_ERROR
	      int wcount = 0, iLGS = largest_grid_size[0];

              while (new_bx.size() < 64 && wcount++ < 4)
              {
                  iLGS /= 2;
                  if (ParallelDescriptor::IOProcessor())
                  {
                      std::cout << "BL_FIX_GATHERV_ERROR:  using iLGS = " << iLGS
                                << "   largest_grid_size was:  " << largest_grid_size[0]
                                << '\n';
                      std::cout << "BL_FIX_GATHERV_ERROR:  new_bx.size() was:   "
                                << new_bx.size() << '\n';
                  }

                  new_bx.maxSize(iLGS);

                  if (ParallelDescriptor::IOProcessor())
                  {
                      std::cout << "BL_FIX_GATHERV_ERROR:  new_bx.size() now:   "
                                << new_bx.size() << '\n';
                  }
	      }
#endif
            //
            // Refine up to levf.
            //
            new_bx.refine(ref_ratio[levc]);
            BL_ASSERT(new_bx.isDisjoint());
            new_grids[levf].define(new_bx);
        }
        //
        // Don't forget to get rid of space used for collate()ing.
        //
        delete [] pts;
    }

    const int NProcs = ParallelDescriptor::NProcs();

    if (NProcs > 1 && refine_grid_layout)
    {
        //
        // Chop up grids if fewer grids at level than CPUs.
        // The idea here is to make more grids on a given level
        // to spread the work around.
        //
        for (int cnt = 1; cnt <= 4; cnt *= 2)
        {
            for (int i = lbase; i <= new_finest; i++)
            {
                const int ChunkSize = max_grid_size[i]/cnt;

                IntVect chunk(D_DECL(ChunkSize,ChunkSize,ChunkSize));

                for (int j = 0; j < BL_SPACEDIM; j++)
                {
                    chunk[j] /= 2;

                    if ((new_grids[i].size() < NProcs) && (chunk[j]%blocking_factor[i] == 0))
                    {
                        new_grids[i].maxSize(chunk);
                    }
                }
            }
        }
    }

    if (verbose > 0)
    {
        const int IOProc   = ParallelDescriptor::IOProcessorNumber();
        Real      stoptime = ParallelDescriptor::second() - strttime;

        ParallelDescriptor::ReduceRealMax(stoptime,IOProc);

        if (ParallelDescriptor::IOProcessor())
        {
            std::cout << "grid_places() time: " << stoptime << '\n';
        }
    }
}

void
Amr::bldFineLevels (Real strt_time)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::bldFineLevels()");
    finest_level = 0;

    Array<BoxArray> grids(max_level+1);
    //
    // Get initial grid placement.
    //
    do
    {
        int new_finest;

        grid_places(finest_level,strt_time,new_finest,grids);

        if (new_finest <= finest_level) break;
        //
        // Create a new level and link with others.
        //
        finest_level = new_finest;

        AmrLevel* level = (*levelbld)(*this,
                                      new_finest,
                                      geom[new_finest],
                                      grids[new_finest],
                                      strt_time);

        amr_level.set(new_finest,level);

        amr_level[new_finest].initData();
    }
    while (finest_level < max_level);
    //
    // Iterate grids to ensure fine grids encompass all interesting gunk.
    //
    bool grids_the_same;

    const int MaxCnt = 4;

    int count = 0;

    do
    {
        for (int i = 0; i <= finest_level; i++)
            grids[i] = amr_level[i].boxArray();

        regrid(0,strt_time,true);

        grids_the_same = true;

        for (int i = 0; i <= finest_level && grids_the_same; i++)
            if (!(grids[i] == amr_level[i].boxArray()))
                grids_the_same = false;

        count++;
    }
    while (!grids_the_same && count < MaxCnt);
}
