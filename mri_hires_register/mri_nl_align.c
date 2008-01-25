/**
 * @file  mri_nl_align.c
 * @brief nonlinear volumetric alignment
 *
 * see (Fischl et al., Neuroimage, 2004)
 */
/*
 * Original Author: Bruce Fischl
 * CVS Revision Info:
 *    $Author: fischl $
 *    $Date: 2008/01/25 23:32:27 $
 *    $Revision: 1.17 $
 *
 * Copyright (C) 2002-2007,
 * The General Hospital Corporation (Boston, MA). 
 * All rights reserved.
 *
 * Distribution, usage and copying of this software is covered under the
 * terms found in the License Agreement file named 'COPYING' found in the
 * FreeSurfer source code root directory, and duplicated here:
 * https://surfer.nmr.mgh.harvard.edu/fswiki/FreeSurferOpenSourceLicense
 *
 * General inquiries: freesurfer@nmr.mgh.harvard.edu
 * Bug reports: analysis-bugs@nmr.mgh.harvard.edu
 *
 */


//
// mri_nl_align.c
//
// written by Bruce Fischl
// Nov. 9th ,2000
// 
// Warning: Do not edit the following four lines.  CVS maintains them.
// Revision Author: $Author: fischl $
// Revision Date  : $Date: 2008/01/25 23:32:27 $
// Revision       : $Revision: 1.17 $
//
////////////////////////////////////////////////////////////////////

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gcamorph.h"
#include "mri.h"
#include "matrix.h"
#include "proto.h"
#include "macros.h"
#include "error.h"
#include "timer.h"
#include "diag.h"
#include "mrimorph.h"
#include "utils.h"
#include "gca.h"
#include "cma.h"
#include "version.h"
#include "transform.h"
#include "fastmarching.h"
#include "voxlist.h"

#define NONMAX 0
#define PAD      10
#define PADVOX   1


static int apply_transform = 1 ;

static int write_snapshot(MRI *mri_target, MRI *mri_source, 
													MATRIX *m_vox_xform, GCA_MORPH_PARMS *parms, 
													int fno, int conform, char *fname) ;

static MRI *create_distance_transforms(MRI *mri_source, MRI *mri_target, 
                                       MRI *mri_all_dtrans, float max_dist,
                                       GCA_MORPH *gcam) ;
static int regrid = 0 ;

static void  usage_exit(int ecode) ;
static int get_option(int argc, char *argv[]) ;


char *Progname ;

static int skip = 2 ;
static double distance = 1.0 ;
static int use_aseg = 0 ;
static int nozero = 1 ;
static int match_peak_intensity_ratio = 0 ;
static int match_mean_intensity = 1 ;


static TRANSFORM  *transform = NULL ;
static GCA_MORPH_PARMS mp ;

static int renormalize = 1 ;

int
main(int argc, char *argv[])
{
	char         **av, *source_fname, *target_fname, *out_fname, fname[STRLEN] ;
  int          ac, nargs, new_transform = 0, pad ;
	MRI          *mri_target, *mri_source, *mri_tmp, *mri_orig_source ;
	MRI_REGION   box ;
  struct timeb start ;
  int          msec, minutes, seconds ;
	GCA_MORPH    *gcam ;
	MATRIX       *m_L/*, *m_I*/ ;
	LTA          *lta ;


	/* initialize the morph params */
	memset(&mp, 0, sizeof(GCA_MORPH_PARMS));
	/* for nonlinear morph */
	mp.l_jacobian = 1 ;
	mp.l_distance = 0 ;
	mp.l_log_likelihood = .025 ;
	mp.dt = 0.005 ;
	mp.noneg = True ;
	mp.exp_k = 20 ;
	mp.diag_write_snapshots = 1 ;
	mp.momentum = 0.9 ;
	if (FZERO(mp.l_smoothness))
		mp.l_smoothness = 2 ;
	mp.sigma = 8 ;
	mp.relabel_avgs = -1 ;
	mp.navgs = 256 ;
	mp.levels = 6 ;
	mp.integration_type = GCAM_INTEGRATE_BOTH ;
	mp.nsmall = 1 ;
	mp.reset_avgs = -1 ;
	mp.npasses = 3 ;
	mp.regrid = regrid? True : False ;
	mp.tol = 0.1 ;
	mp.niterations = 1000 ;
	
  TimerStart(&start) ;
  setRandomSeed(-1L) ;
  DiagInit(NULL, NULL, NULL) ;
  ErrorInit(NULL, NULL, NULL) ;

	Progname = argv[0] ;
  ac = argc ;
  av = argv ;
  for ( ; argc > 1 && ISOPTION(*argv[1]) ; argc--, argv++)
  {
    nargs = get_option(argc, argv) ;
    argc -= nargs ;
    argv += nargs ;
  }

  if (argc < 4)
		usage_exit(1) ;

	source_fname = argv[1] ;
	target_fname = argv[2] ;
	out_fname = argv[3] ;
  FileNameOnly(out_fname, fname) ;
  FileNameRemoveExtension(fname, fname) ;
  strcpy(mp.base_name, fname) ;
	mri_source = MRIread(source_fname) ;
	if (!mri_source)
		ErrorExit(ERROR_NOFILE, "%s: could not read source label volume %s",
							Progname, source_fname) ;

  if (mri_source->type == MRI_INT)
  {
    MRI *mri_tmp = MRIchangeType(mri_source, MRI_FLOAT, 0, 1, 1) ;
    MRIfree(&mri_source); mri_source = mri_tmp ;
  }
	mri_target = MRIread(target_fname) ;
	if (!mri_target)
		ErrorExit(ERROR_NOFILE, "%s: could not read target label volume %s",
							Progname, target_fname) ;
  if (mri_target->type == MRI_INT)
  {
    MRI *mri_tmp = MRIchangeType(mri_target, MRI_FLOAT, 0, 1, 1) ;
    MRIfree(&mri_target); mri_target = mri_tmp ;
  }
  if (use_aseg == 0)
  {
    if (match_peak_intensity_ratio)
      MRImatchIntensityRatio(mri_source, mri_target, mri_source, .8, 1.2, 
                             100, 125) ;
  else if (match_mean_intensity)
    MRImatchMeanIntensity(mri_source, mri_target, mri_source) ;
    MRIboundingBox(mri_source, 0, &box) ;
    pad = (int)ceil(PADVOX * 
                    MAX(mri_target->xsize,MAX(mri_target->ysize,mri_target->zsize)) / 
                    MIN(mri_source->xsize,MIN(mri_source->ysize,mri_source->zsize))); 
    if (pad < 1)
      pad = 1 ;
    printf("padding source with %d voxels...\n", pad) ;
    mri_tmp = MRIextractRegionAndPad(mri_source, NULL, &box, pad) ;
    if ((Gdiag & DIAG_WRITE) && DIAG_VERBOSE_ON)
      MRIwrite(mri_tmp, "t.mgz") ;
    MRIfree(&mri_source) ;
    mri_source = mri_tmp ;
  }
	mri_orig_source = MRIcopy(mri_source, NULL) ;

	mp.max_grad = 0.3*mri_source->xsize ;

	if (transform == NULL)
    transform = TransformAlloc(LINEAR_VOXEL_TO_VOXEL, NULL) ;

	if (transform->type != MORPH_3D_TYPE)  // initializing m3d from a linear transform
	{
		new_transform = 1 ;
		lta = ((LTA *)(transform->xform)) ;
		if (lta->type != LINEAR_VOX_TO_VOX)
		{
			printf("converting ras xform to voxel xform\n") ;
			m_L = MRIrasXformToVoxelXform(mri_source, mri_target, lta->xforms[0].m_L, NULL) ;
			MatrixFree(&lta->xforms[0].m_L) ;
		}
		else
		{
			printf("using voxel xform\n") ;
			m_L = lta->xforms[0].m_L ;
		}
#if 0
    if (Gsx >= 0)   // update debugging coords
    {
      VECTOR *v1, *v2 ;

      v1 = VectorAlloc(4, MATRIX_REAL) ;
      Gsx -= (box.x-pad) ;
      Gsy -= (box.y-pad) ;
      Gsz -= (box.z-pad) ;
      V3_X(v1) = Gsx ; V3_Y(v1) = Gsy ; V3_Z(v1) = Gsz ;
      VECTOR_ELT(v1,4) = 1.0 ;
      v2 = MatrixMultiply(m_L, v1, NULL) ;
      
      Gsx = nint(V3_X(v2)) ; Gsy = nint(V3_Y(v2)) ; Gsz = nint(V3_Z(v2)) ;
      MatrixFree(&v2) ; MatrixFree(&v1) ;
      printf("mapping by transform (%d, %d, %d) --> (%d, %d, %d) for rgb writing\n",
             Gx, Gy, Gz, Gsx, Gsy, Gsz) ;
    }
#endif
		if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON)
			write_snapshot(mri_target, mri_source, m_L, &mp, 0, 1, "linear_init");

		lta->xforms[0].m_L = m_L ;
		printf("initializing GCAM with vox->vox matrix:\n") ;
		MatrixPrint(stdout, m_L) ;
		gcam = GCAMcreateFromIntensityImage(mri_source, mri_target, transform) ;
    if (use_aseg)
    {
      GCAMinitLabels(gcam, mri_source) ;
      GCAMsetVariances(gcam, 1.0) ;
      mp.mri_dist_map = create_distance_transforms(mri_source, mri_target, NULL, 10.0, gcam) ;
    }
	}
	else  /* use a previously create morph and integrate it some more */
	{
		printf("using previously create gcam...\n") ;
		gcam = (GCA_MORPH *)(transform->xform) ;
		GCAMrasToVox(gcam, mri_target) ;
	}
	if (gcam->width != mri_source->width ||
			gcam->height != mri_source->height ||
			gcam->depth != mri_source->depth)
		ErrorExit(ERROR_BADPARM, "%s: warning gcam (%d, %d, %d), doesn't match source vol (%d, %d, %d)",
							Progname, gcam->width, gcam->height, gcam->depth,
							mri_source->width, mri_source->height, mri_source->depth) ;
	
	mp.mri_diag = NULL ;
	mp.diag_morph_from_atlas = 0 ;
	mp.diag_volume = 0 ;
	
	mp.mri_diag = mri_target ;
	mp.diag_morph_from_atlas = 0 ;
	mp.diag_write_snapshots = 1 ;
  mp.diag_sample_type = use_aseg ? SAMPLE_NEAREST : SAMPLE_TRILINEAR ;
	mp.diag_volume = use_aseg ? GCAM_LABEL : GCAM_MEANS ;

  if (renormalize)
    GCAMnormalizeIntensities(gcam, mri_target) ;
	if (mp.write_iterations != 0)
	{
		char fname[STRLEN] ;
		MRI  *mri_gca ;
		
		sprintf(fname, "%s_target.mgz", mp.base_name) ;
		if (mp.diag_morph_from_atlas)
		{
			printf("writing target volume to %s...\n", fname) ;
			MRIwrite(mri_target, fname) ;
			sprintf(fname, "%s_target", mp.base_name) ;
			MRIwriteImageViews(mri_target, fname, IMAGE_SIZE) ;
		}
		else
		{
			mri_gca = MRIclone(mri_source, NULL) ;
			GCAMbuildMostLikelyVolume(gcam, mri_gca) ;
			printf("writing target volume to %s...\n", fname) ;
			MRIwrite(mri_gca, fname) ;
			sprintf(fname, "%s_target", mp.base_name) ;
			MRIwriteImageViews(mri_gca, fname, IMAGE_SIZE) ;
			MRIfree(&mri_gca) ;
		}
	}

	if (nozero)
	{
		printf("disabling zero nodes\n") ;
		GCAMignoreZero(gcam, mri_source, mri_target) ;
	}
	mp.mri = mri_target ;
	if (mp.regrid == True && new_transform == 0)
		GCAMregrid(gcam, mri_target, PAD, &mp, &mri_source) ;

  mp.write_fname = out_fname ;
	GCAMregister(gcam, mri_target, &mp) ; // atlas is source, morph target into register with it
	if (apply_transform)
	{
		MRI *mri_aligned ;
		char   fname[STRLEN] ;
		
		FileNameRemoveExtension(out_fname, fname) ;
		strcat(fname, ".mgz") ;
		mri_aligned = GCAMmorphFieldFromAtlas(gcam, mp.mri, GCAM_MEANS,0, 1) ;
		printf("writing transformed output volume to %s...\n", fname) ;
		MRIwrite(mri_aligned, fname) ;
		MRIfree(&mri_aligned) ;
	}
	printf("writing warp vector field to %s\n", out_fname) ;
	GCAMvoxToRas(gcam) ;
	GCAMwrite(gcam, out_fname) ;
	GCAMrasToVox(gcam, mri_target) ;

  msec = TimerStop(&start) ;
  seconds = nint((float)msec/1000.0f) ;
  minutes = seconds / 60 ;
  seconds = seconds % 60 ;
  printf("registration took %d minutes and %d seconds.\n", 
				 minutes, seconds) ;
	exit(0) ;
	return(0) ;
}

static int
get_option(int argc, char *argv[])
{
  int  nargs = 0 ;
  char *option ;

  option = argv[1] + 1 ;            /* past '-' */
  StrUpper(option) ;
  if (!stricmp(option, "debug_voxel"))
	{
		Gsx = Gx = atoi(argv[2]) ;
		Gsy = Gy = atoi(argv[3]) ;
		Gsz = Gz = atoi(argv[4]) ;
		nargs = 3 ;
		printf("debugging voxel (%d, %d, %d)\n", Gx, Gy, Gz) ;
	}
  else if (!stricmp(option, "debug_node"))
	{
		Gx = atoi(argv[2]) ;
		Gy = atoi(argv[3]) ;
		Gz = atoi(argv[4]) ;
		nargs = 3 ;
		printf("debugging node (%d, %d, %d)\n", Gx, Gy, Gz) ;
	}
  else if (!stricmp(option, "OPTIMAL"))
  {
    mp.integration_type = GCAM_INTEGRATE_OPTIMAL ;
    printf("using optimal time-step integration\n") ;
  }
  else if (!stricmp(option, "aseg"))
  {
    match_mean_intensity = match_peak_intensity_ratio = 0 ;
    use_aseg = 1 ;
    mp.l_dtrans = 1 ;
    mp.l_log_likelihood = 0 ;
    renormalize = 0 ;
    printf("treating inputs as segmentations\n") ;
  }
  else if (!stricmp(option, "MOMENTUM") || !stricmp(option, "FIXED"))
  {
    mp.integration_type = GCAM_INTEGRATE_FIXED ;
    printf("using optimal time-step integration\n") ;
  }
  else if (!stricmp(option, "distance"))
  {
    distance = atof(argv[2]) ;
		nargs = 1 ;
    printf("expanding border by %2.1f mm every outer cycle\n", distance);
  }
  else if (!stricmp(option, "dtrans"))
  {
    mp.l_dtrans = atof(argv[2]) ;
		nargs = 1 ;
    printf("setting distance transform coefficient to %2.3f\n", mp.l_dtrans) ;
  }
  else if (!stricmp(option, "match_peak"))
  {
    match_peak_intensity_ratio = 1 ;
    match_mean_intensity = 0 ;
    printf("matching peak of intensity ratio histogram\n") ;
  }
  else if (!stricmp(option, "match_mean"))
  {
    match_peak_intensity_ratio = 0 ;
    match_mean_intensity = atoi(argv[2]) ;
    nargs = 1 ;
    printf("%smatching peak of intensity ratio histogram\n",
           match_mean_intensity ? "" : "not ") ;
  }
  else if (!stricmp(option, "intensity") ||!stricmp(option, "ll"))
  {
    mp.l_log_likelihood = atof(argv[2]) ;
		nargs = 1 ;
    printf("setting l_log_likelihood = %2.3f\n", mp.l_log_likelihood );
  }
  else if (!stricmp(option, "noregrid"))
  {
		regrid = 0 ;
		mp.regrid = False ;
    printf("disabling regridding...\n") ;
  }
  else if (!stricmp(option, "regrid"))
  {
		regrid = 1 ;
		mp.regrid = True ;
    printf("enabling regridding...\n") ;
  }
  else if (!stricmp(option, "view"))
	{
		Gsx = atoi(argv[2]) ;
		Gsy = atoi(argv[3]) ;
		Gsz = atoi(argv[4]) ;
		nargs = 3 ;
		printf("viewing voxel (%d, %d, %d)\n", Gsx, Gsy, Gsz) ;
	}
  else if (!stricmp(option, "LEVELS"))
  {
    mp.levels = atoi(argv[2]) ;
    nargs = 1 ;
    printf("levels = %d\n", mp.levels) ;
  }
  else if (!stricmp(option, "area_smoothness") || !stricmp(option, "asmooth"))
  {
    mp.l_area_smoothness = atof(argv[2]) ;
    nargs = 1 ;
    printf("using l_area_smoothness=%2.3f\n", mp.l_area_smoothness) ;
  }
  else if (!stricmp(option, "area"))
  {
    mp.l_area = atof(argv[2]) ;
    nargs = 1 ;
    printf("using l_area=%2.3f\n", mp.l_area) ;
  }
  else if (!stricmp(option, "area_intensity"))
  {
    mp.l_area_intensity = atof(argv[2]) ;
    nargs = 1 ;
    printf("using l_area_intensity=%2.3f\n", mp.l_area_intensity) ;
  }
  else if (!stricmp(option, "tol"))
  {
    mp.tol = atof(argv[2]) ;
    nargs = 1 ;
    printf("using tol=%2.3f\n", mp.tol) ;
  }
  else if (!stricmp(option, "sigma"))
  {
    mp.sigma = atof(argv[2]) ;
    nargs = 1 ;
    printf("using sigma=%2.3f\n", mp.sigma) ;
  }
	else if (!stricmp(option, "rthresh"))
  {
		mp.ratio_thresh = atof(argv[2]) ;
    mp.uncompress = 1 ;
    nargs = 1 ;
    printf("using compression ratio threshold = %2.3f...\n", mp.ratio_thresh) ;
  }
	else if (!stricmp(option, "dt"))
	{
		mp.dt = atof(argv[2]) ;
		nargs = 1 ;
		printf("using dt = %2.3f\n", mp.dt) ;
	}
	else if (!stricmp(option, "passes"))
	{
		mp.npasses = atoi(argv[2]) ;
		nargs = 1 ;
		printf("integrating in %d passes (default=3)\n", mp.npasses) ;
	}
	else if (!stricmp(option, "skip"))
	{
		skip = atoi(argv[2]);
		printf("skipping %d voxels in source data...\n", skip) ;
		nargs = 1 ;
	}
	else if (!stricmp(option, "apply"))
	{
	  apply_transform = atoi(argv[2]) ;
	  nargs = 1 ;
	  printf("%sapplying transform after registration\n", apply_transform ? "" : "not ") ;
  }
	else switch (*option)
	{
	case 'D':
		mp.l_distance = atof(argv[2]) ;
		nargs = 1 ;
		printf("using l_distance = %2.3f\n", mp.l_distance) ;
		break ;
  case 'M':
    mp.momentum = atof(argv[2]) ;
    nargs = 1 ;
    printf("momentum = %2.2f\n", mp.momentum) ;
    break ;
  case 'R':
    renormalize = atoi(argv[2]) ;
    printf("%srenormalizing intensities\n", renormalize ? "" : "not ") ;
    nargs = 1 ;
    break ;
	case 'N':
		mp.niterations = atoi(argv[2]) ;
		nargs = 1 ;
		printf("using niterations = %d\n", mp.niterations) ;
		break ;
	case 'S':
		mp.l_smoothness = atof(argv[2]) ;
		nargs = 1 ;
		printf("using l_smoothness = %2.3f\n", mp.l_smoothness) ;
		break ;
	case 'T':
		printf("reading transform from %s...\n", argv[2]) ;
		transform = TransformRead(argv[2]) ;
		if (transform == NULL)
			ErrorExit(ERROR_NOFILE,"%s: could not read transform from %s\n",Progname,argv[2]);
		nargs = 1 ;
    if (transform->type == LINEAR_VOX_TO_VOX)
    {
      printf("converting transform to ras....\n") ;
      LTAvoxelToRasXform((LTA *)(transform->xform), NULL, NULL) ;
    }
		break ;
	case 'I':
		printf("reading transform from %s...\n", argv[2]) ;
		transform = TransformRead(argv[2]) ;
		if (transform == NULL)
			ErrorExit(ERROR_NOFILE,"%s: could not read transform from %s\n",Progname,argv[2]);
    TransformInvert(transform, NULL) ;
    TransformSwapInverse(transform) ;
    if (transform->type == LINEAR_VOX_TO_VOX)
    {
      printf("converting transform to ras....\n") ;
      LTAvoxelToRasXform((LTA *)(transform->xform), NULL, NULL) ;
    }
		nargs = 1 ;
		break ;
	case 'B':
    mp.l_binary = atof(argv[2]) ;
    nargs = 1 ;
    printf("using l_binary=%2.3f\n", mp.l_binary) ;
    break ;
  case 'J':
    mp.l_jacobian = atof(argv[2]) ;
    nargs = 1 ;
    printf("using l_jacobian=%2.3f\n", mp.l_jacobian) ;
    break ;
  case 'Z':
    nozero = (atoi(argv[2]) == 0) ;
    printf("%sdisabling zero image locations\n", nozero ? "" : "not ") ;
    nargs = 1 ;
    break ;
  case 'A':
		mp.navgs = atoi(argv[2]) ;
    nargs = 1 ;
    printf("smoothing gradient with %d averages...\n", mp.navgs) ;
    break ;
  case 'K':
    mp.exp_k = atof(argv[2]) ;
    printf("setting exp_k to %2.2f (default=%2.2f)\n",
           mp.exp_k, EXP_K) ;
    nargs = 1 ;
    break ;
	case 'W':
		mp.write_iterations = atoi(argv[2]) ;
		Gdiag |= DIAG_WRITE ;
		nargs = 1 ;
		printf("setting write iterations = %d\n", mp.write_iterations) ;
		break ;
  case '?':
  case 'U':
    usage_exit(1);
    break ;
	default:
    printf("unknown option %s\n", argv[1]) ;
		usage_exit(1) ;
    break ;
	}
	return(nargs) ;
}

static void 
usage_exit(int ecode)
{
	printf(
				 "usage: %s <source> <destination> <output xform>\n",
				 Progname) ;
	exit(ecode) ;
}


static int
write_snapshot(MRI *mri_target, MRI *mri_source, MATRIX *m_vox_xform, 
							 GCA_MORPH_PARMS *parms, int fno, int conform, char *in_fname)
{
	MRI *mri_aligned ;
	char fname[STRLEN] ;

	if (Gdiag & DIAG_SHOW && DIAG_VERBOSE_ON)
	{
		printf("source->target vox->vox transform:\n") ;
		MatrixPrint(stdout, m_vox_xform) ;
	}
	if (conform || 1)
	{
		mri_aligned = MRIalloc(mri_target->width, mri_target->height,
													 mri_target->depth,mri_source->type);
		MRIcopyHeader(mri_target, mri_aligned) ;
		MRIlinearTransformInterp(mri_source, mri_aligned, m_vox_xform, SAMPLE_NEAREST);
	}
	else
	{
		mri_aligned = MRITransformedCenteredMatrix(mri_source, mri_target, m_vox_xform) ;
	}
	if (in_fname)
		sprintf(fname, "%s_%s", parms->base_name, in_fname) ;
	else
		sprintf(fname, "%s_%03d", parms->base_name, fno) ;
	MRIwriteImageViews(mri_aligned, fname, IMAGE_SIZE) ;
	if (in_fname)
		sprintf(fname, "%s_%s.mgz", parms->base_name, in_fname) ;
	else
		sprintf(fname, "%s_%03d.mgz", parms->base_name, fno) ;
	printf("writing snapshot to %s...\n", fname) ;
	MRIwrite(mri_aligned, fname) ;
	MRIfree(&mri_aligned) ;

	{
#if 0
		mri_aligned = MRIsrcTransformedCentered(mri_source, mri_target, m_vox_xform,
																						SAMPLE_NEAREST) ;
#else
		mri_aligned = MRITransformedCenteredMatrix(mri_source, mri_target, m_vox_xform) ;
#endif
		if (in_fname)
			sprintf(fname, "orig_%s_%s.mgz", parms->base_name, in_fname) ;
		else
			sprintf(fname, "orig_%s_%03d.mgz", parms->base_name, fno) ;
		printf("writing snapshot to %s...\n", fname) ;
		MRIwrite(mri_aligned, fname) ;
		MRIfree(&mri_aligned) ;
	}

	return(NO_ERROR) ;
}

// NOTE: the order here *must* match the frame #s listed in gcamorph.c:dtrans_label_to_frame
static int dtrans_labels[] =
  {
    Left_Thalamus_Proper,
    Right_Thalamus_Proper,
    Left_Putamen,
    Right_Putamen,
    Left_Pallidum,
    Right_Pallidum,
    Left_Lateral_Ventricle,
    Right_Lateral_Ventricle,
    Left_Caudate,
    Right_Caudate,
    Left_Cerebral_White_Matter,
    Right_Cerebral_White_Matter,
    Left_Hippocampus,
    Right_Hippocampus,
    Left_Amygdala,
    Right_Amygdala,
    Left_VentralDC,
    Right_VentralDC,
    Brain_Stem,
    Left_Inf_Lat_Vent,
    Right_Inf_Lat_Vent
  } ;

#define NDTRANS_LABELS (sizeof(dtrans_labels) / sizeof(dtrans_labels[0]))

static MRI *
create_distance_transforms(MRI *mri_source, MRI *mri_target, MRI *mri_all_dtrans, float max_dist, GCA_MORPH *gcam)
{
  MRI   *mri_dtrans ;
  int   frame ;
  char  fname[STRLEN] ;
  
  mri_all_dtrans = MRIallocSequence(mri_target->width, mri_target->height, mri_target->depth, 
                                    MRI_FLOAT, NDTRANS_LABELS) ;
  MRIcopyHeader(mri_target, mri_all_dtrans) ;

  for (frame = 0 ; frame < NDTRANS_LABELS ; frame++)
  {
    printf("creating distance transform for %s, frame %d...\n", cma_label_to_name(dtrans_labels[frame]), frame) ;
    mri_dtrans = MRIdistanceTransform(mri_target, NULL, dtrans_labels[frame], 10, DTRANS_MODE_SIGNED) ;
    sprintf(fname, "%s.mgz", cma_label_to_name(dtrans_labels[frame])) ;
    if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON)
      MRIwrite(mri_dtrans, fname) ;
    MRIcopyFrame(mri_dtrans, mri_all_dtrans, 0, frame) ;
    MRIfree(&mri_dtrans) ;
    mri_dtrans = MRIdistanceTransform(mri_source, NULL, dtrans_labels[frame], 10, DTRANS_MODE_SIGNED) ;
    GCAMsetMeansForLabel(gcam, mri_source, mri_dtrans, dtrans_labels[frame]);
    MRIfree(&mri_dtrans) ;
  }

  return(mri_all_dtrans) ;
}

