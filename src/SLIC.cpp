// SLIC.cpp: implementation of the SLIC class.
//===========================================================================
// This code implements the zero parameter superpixel segmentation technique
// described in:
//
//
//
// "SLIC Superpixels Compared to State-of-the-art Superpixel Methods"
//
// Radhakrishna Achanta, Appu Shaji, Kevin Smith, Aurelien Lucchi, Pascal Fua,
// and Sabine Susstrunk,
//
// IEEE TPAMI, Volume 34, Issue 11, Pages 2274-2282, November 2012.
//
// https://www.epfl.ch/labs/ivrl/research/slic-superpixels/
//===========================================================================
// Copyright (c) 2013 Radhakrishna Achanta.
//
// For commercial use please contact the author:
//
// Email: firstname.lastname@epfl.ch
//===========================================================================

#include <stdio.h>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <fstream>
#include "SLIC.h"
#include <chrono>
#include <immintrin.h>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <map>
#include <deque>
#include <omp.h>

typedef std::chrono::high_resolution_clock Clock;

// For superpixels
const int dx4[4] = {-1, 0, 1, 0};
const int dy4[4] = {0, -1, 0, 1};
//const int dx8[8] = {-1, -1,  0,  1, 1, 1, 0, -1};
//const int dy8[8] = { 0, -1, -1, -1, 0, 1, 1,  1};

// For supervoxels
const int dx10[10] = {-1, 0, 1, 0, -1, 1, 1, -1, 0, 0};
const int dy10[10] = {0, -1, 0, 1, -1, -1, 1, 1, 0, 0};
const int dz10[10] = {0, 0, 0, 0, 0, 0, 0, 0, -1, 1};

#if _OPENMP
struct my_max
{
	template <class T>
	const T &operator()(const T &a, const T &b) const
	{
		return std::max(a, b);
	}
};
#pragma omp declare reduction(vec_double_sum                                                                                           \
							  : std::vector <double>                                                                                   \
							  : std::transform(omp_out.begin(), omp_out.end(), omp_in.begin(), omp_out.begin(), std::plus <double>())) \
	initializer(omp_priv = decltype(omp_orig)(omp_orig.size()))
#pragma omp declare reduction(vec_double_max                                                                               \
							  : std::vector <double>                                                                       \
							  : std::transform(omp_out.begin(), omp_out.end(), omp_in.begin(), omp_out.begin(), my_max())) \
	initializer(omp_priv = decltype(omp_orig)(omp_orig.size()))
#pragma omp declare reduction(vec_int_sum                                                                                           \
							  : std::vector <int>                                                                                   \
							  : std::transform(omp_out.begin(), omp_out.end(), omp_in.begin(), omp_out.begin(), std::plus <int>())) \
	initializer(omp_priv = decltype(omp_orig)(omp_orig.size()))
#pragma omp declare reduction(merge                     \
							  : std::vector <area_info> \
							  : omp_out.insert(omp_out.end(), omp_in.begin(), omp_in.end()))
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

SLIC::SLIC()
{
	m_lvec = NULL;
	m_avec = NULL;
	m_bvec = NULL;

	m_lvecvec = NULL;
	m_avecvec = NULL;
	m_bvecvec = NULL;
}

SLIC::~SLIC()
{
	if (m_lvec)
		delete[] m_lvec;
	if (m_avec)
		delete[] m_avec;
	if (m_bvec)
		delete[] m_bvec;

	if (m_lvecvec)
	{
		for (int d = 0; d < m_depth; d++)
			delete[] m_lvecvec[d];
		delete[] m_lvecvec;
	}
	if (m_avecvec)
	{
		for (int d = 0; d < m_depth; d++)
			delete[] m_avecvec[d];
		delete[] m_avecvec;
	}
	if (m_bvecvec)
	{
		for (int d = 0; d < m_depth; d++)
			delete[] m_bvecvec[d];
		delete[] m_bvecvec;
	}
}

//==============================================================================
///	RGB2XYZ
///
/// sRGB (D65 illuninant assumption) to XYZ conversion
//==============================================================================
void SLIC::RGB2XYZ(
	const int &sR,
	const int &sG,
	const int &sB,
	double &X,
	double &Y,
	double &Z)
{
	double R = sR / 255.0;
	double G = sG / 255.0;
	double B = sB / 255.0;

	double r, g, b;

	if (R <= 0.04045)
		r = 0.6;
	// r = R / 12.92;
	else
		// r = pow((R + 0.055) / 1.055, 2.4);
		r = 0.5;
	if (G <= 0.04045)
		g = 0.6;
	// g = G / 12.92;
	else
		// 	g = pow((G + 0.055) / 1.055, 2.4);
		g = 0.5;
	if (B <= 0.04045)
		b = 0.6;
	// b = B / 12.92;
	else
		// 	b = pow((B + 0.055) / 1.055, 2.4);
		b = 0.5;

	X = r * 0.4124564 + g * 0.3575761 + b * 0.1804375;
	Y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750;
	Z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041;
}

//===========================================================================
///	RGB2LAB
//===========================================================================
void SLIC::RGB2LAB(const int &sR, const int &sG, const int &sB, double &lval, double &aval, double &bval)
{
	//------------------------
	// sRGB to XYZ conversion
	//------------------------
	double X, Y, Z;
	RGB2XYZ(sR, sG, sB, X, Y, Z);

	//------------------------
	// XYZ to LAB conversion
	//------------------------
	double epsilon = 0.008856; //actual CIE standard
	double kappa = 903.3;	   //actual CIE standard

	double Xr = 0.950456; //reference white
	double Yr = 1.0;	  //reference white
	double Zr = 1.088754; //reference white

	double xr = X / Xr;
	double yr = Y / Yr;
	double zr = Z / Zr;

	double fx, fy, fz;
	double fx2, fy2, fz2;
	fx2 = (kappa * xr + 16.0) / 116.0;
	fy2 = (kappa * yr + 16.0) / 116.0;
	fz2 = (kappa * zr + 16.0) / 116.0;
	fx = cbrt(xr);
	fy = cbrt(yr);
	fz = cbrt(zr);
	fx = xr > epsilon ? fx : fx2;
	fy = yr > epsilon ? fy : fy2;
	fz = zr > epsilon ? fz : fz2;

	lval = 116.0 * fy - 16.0;
	aval = 500.0 * (fx - fy);
	bval = 200.0 * (fy - fz);
}

//===========================================================================
///	DoRGBtoLABConversion
///
///	For whole image: overlaoded floating point version
//===========================================================================
void SLIC::DoRGBtoLABConversion(
	const unsigned int *&ubuff,
	double *&lvec,
	double *&avec,
	double *&bvec)
{
	int sz = m_width * m_height;
	lvec = new double[sz];
	avec = new double[sz];
	bvec = new double[sz];
// #pragma prefetch rgb_lut : 2 : 256
// #pragma prefetch rgb_pow_lut : 2 : 256
// #pragma prefetch ubuff : 1 : 16
#if _OPENMP
#pragma omp parallel for simd
#endif
	for (int j = 0; j < sz; j++)
	{
		int sR = (ubuff[j] >> 16) & 0xFF;
		int sG = (ubuff[j] >> 8) & 0xFF;
		int sB = (ubuff[j]) & 0xFF;

		//------------------------
		// sRGB to XYZ conversion
		//------------------------
		double X, Y, Z;
		// double R = sR / 255.0;
		// double G = sG / 255.0;
		// double B = sB / 255.0;

		double r, g, b;

		if (sR <= 10.31475)
			r = rgb_lut[sR];
		else
			r = rgb_pow_lut[sR];
		if (sG <= 10.31475)
			g = rgb_lut[sG];
		else
			g = rgb_pow_lut[sG];
		if (sB <= 10.31475)
			b = rgb_lut[sB];
		else
			b = rgb_pow_lut[sB];

		X = r * 0.4124564 + g * 0.3575761 + b * 0.1804375;
		Y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750;
		Z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041;

		//------------------------
		// XYZ to LAB conversion
		//------------------------
		double epsilon = 0.008856; //actual CIE standard
		double kappa = 903.3;	   //actual CIE standard

		double Xr = 0.950456; //reference white
		double Yr = 1.0;	  //reference white
		double Zr = 1.088754; //reference white

		double xr = X / Xr;
		double yr = Y / Yr;
		double zr = Z / Zr;

		double fx, fy, fz;
		double fx2, fy2, fz2;
		fx2 = (kappa * xr + 16.0) / 116.0;
		fy2 = (kappa * yr + 16.0) / 116.0;
		fz2 = (kappa * zr + 16.0) / 116.0;
		fx = cbrt(xr);
		fy = cbrt(yr);
		fz = cbrt(zr);
		fx = xr > epsilon ? fx : fx2;
		fy = yr > epsilon ? fy : fy2;
		fz = zr > epsilon ? fz : fz2;

		lvec[j] = 116.0 * fy - 16.0;
		avec[j] = 500.0 * (fx - fy);
		bvec[j] = 200.0 * (fy - fz);
	}
}

//==============================================================================
///	DetectLabEdges
//==============================================================================
void SLIC::DetectLabEdges(
	const double *lvec,
	const double *avec,
	const double *bvec,
	const int &width,
	const int &height,
	vector<double> &edges)
{
	int sz = width * height;

	edges.resize(sz);
#pragma omp parallel for simd
	for (int j = 1; j < height - 1; j++)
	{
		for (int k = 1; k < width - 1; k++)
		{
			int i = j * width + k;

			double dx = (lvec[i - 1] - lvec[i + 1]) * (lvec[i - 1] - lvec[i + 1]) +
						(avec[i - 1] - avec[i + 1]) * (avec[i - 1] - avec[i + 1]) +
						(bvec[i - 1] - bvec[i + 1]) * (bvec[i - 1] - bvec[i + 1]);

			double dy = (lvec[i - width] - lvec[i + width]) * (lvec[i - width] - lvec[i + width]) +
						(avec[i - width] - avec[i + width]) * (avec[i - width] - avec[i + width]) +
						(bvec[i - width] - bvec[i + width]) * (bvec[i - width] - bvec[i + width]);

			//edges[i] = (sqrt(dx) + sqrt(dy));
			edges[i] = (dx + dy);
		}
	}
}
double SLIC::DetectLABPixelEdge(
	const int &i)
{
	const double *lvec = m_lvec;
	const double *avec = m_avec;
	const double *bvec = m_bvec;
	const int width = m_width;

	double dx = (lvec[i - 1] - lvec[i + 1]) * (lvec[i - 1] - lvec[i + 1]) +
				(avec[i - 1] - avec[i + 1]) * (avec[i - 1] - avec[i + 1]) +
				(bvec[i - 1] - bvec[i + 1]) * (bvec[i - 1] - bvec[i + 1]);

	double dy = (lvec[i - width] - lvec[i + width]) * (lvec[i - width] - lvec[i + width]) +
				(avec[i - width] - avec[i + width]) * (avec[i - width] - avec[i + width]) +
				(bvec[i - width] - bvec[i + width]) * (bvec[i - width] - bvec[i + width]);
	return dx + dy;
}

//===========================================================================
///	PerturbSeeds
//===========================================================================
void SLIC::PerturbSeeds(
	vector<double> &kseedsl,
	vector<double> &kseedsa,
	vector<double> &kseedsb,
	vector<double> &kseedsx,
	vector<double> &kseedsy,
	const vector<double> &edges)
{
	const int dx8[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
	const int dy8[8] = {0, -1, -1, -1, 0, 1, 1, 1};

	int numseeds = kseedsl.size();

	for (int n = 0; n < numseeds; n++)
	{
		int ox = kseedsx[n]; //original x
		int oy = kseedsy[n]; //original y
		int oind = oy * m_width + ox;

		int storeind = oind;
		for (int i = 0; i < 8; i++)
		{
			int nx = ox + dx8[i]; //new x
			int ny = oy + dy8[i]; //new y

			if (nx >= 0 && nx < m_width && ny >= 0 && ny < m_height)
			{
				int nind = ny * m_width + nx;
				if (DetectLABPixelEdge(nind) < DetectLABPixelEdge(storeind))
				{
					storeind = nind;
				}
			}
		}
		if (storeind != oind)
		{
			kseedsx[n] = storeind % m_width;
			kseedsy[n] = storeind / m_width;
			kseedsl[n] = m_lvec[storeind];
			kseedsa[n] = m_avec[storeind];
			kseedsb[n] = m_bvec[storeind];
		}
	}
}

//===========================================================================
///	GetLABXYSeeds_ForGivenK
///
/// The k seed values are taken as uniform spatial pixel samples.
//===========================================================================
void SLIC::GetLABXYSeeds_ForGivenK(
	vector<double> &kseedsl,
	vector<double> &kseedsa,
	vector<double> &kseedsb,
	vector<double> &kseedsx,
	vector<double> &kseedsy,
	const int &K,
	const bool &perturbseeds,
	const vector<double> &edgemag)
{
	int sz = m_width * m_height;
	double step = sqrt(double(sz) / double(K));
	int T = step;
	int xoff = step / 2;
	int yoff = step / 2;

	int n(0);
	int r(0);
	for (int y = 0; y < m_height; y++)
	{
		int Y = y * step + yoff;
		if (Y > m_height - 1)
			break;

		for (int x = 0; x < m_width; x++)
		{
			//int X = x*step + xoff;//square grid
			int X = x * step + (xoff << (r & 0x1)); //hex grid
			if (X > m_width - 1)
				break;

			int i = Y * m_width + X;

			//_ASSERT(n < K);

			//kseedsl[n] = m_lvec[i];
			//kseedsa[n] = m_avec[i];
			//kseedsb[n] = m_bvec[i];
			//kseedsx[n] = X;
			//kseedsy[n] = Y;
			kseedsl.push_back(m_lvec[i]);
			kseedsa.push_back(m_avec[i]);
			kseedsb.push_back(m_bvec[i]);
			kseedsx.push_back(X);
			kseedsy.push_back(Y);
			n++;
		}
		r++;
	}

	if (perturbseeds)
	{
		PerturbSeeds(kseedsl, kseedsa, kseedsb, kseedsx, kseedsy, edgemag);
	}
}

//===========================================================================
///	PerformSuperpixelSegmentation_VariableSandM
///
///	Magic SLIC - no parameters
///
///	Performs k mean segmentation. It is fast because it looks locally, not
/// over the entire image.
/// This function picks the maximum value of color distance as compact factor
/// M and maximum pixel distance as grid step size S from each cluster (13 April 2011).
/// So no need to input a constant value of M and S. There are two clear
/// advantages:
///
/// [1] The algorithm now better handles both textured and non-textured regions
/// [2] There is not need to set any parameters!!!
///
/// SLICO (or SLIC Zero) dynamically varies only the compactness factor S,
/// not the step size S.
//===========================================================================
void SLIC::PerformSuperpixelSegmentation_VariableSandM(
	vector<double> &kseedsl,
	vector<double> &kseedsa,
	vector<double> &kseedsb,
	vector<double> &kseedsx,
	vector<double> &kseedsy,
	int *klabels,
	const int &STEP,
	const int &NUMITR)
{
	int sz = m_width * m_height;
	const int numk = kseedsl.size();
	//double cumerr(99999.9);
	// int numitr(0);

	//----------------
	int offset = STEP;
	if (STEP < 10)
		offset = STEP * 1.5;
	//----------------

	vector<double> sigmal(numk, 0);
	vector<double> sigmaa(numk, 0);
	vector<double> sigmab(numk, 0);
	vector<double> sigmax(numk, 0);
	vector<double> sigmay(numk, 0);
	vector<int> clustersize(numk, 0);
	vector<double> inv(numk, 0);   //to store 1/clustersize[k] values
	auto distlab = new double[sz]; // Do not init, and do not use vector
	// vector<double> distvec(sz, DBL_MAX);
	auto distvec = new double[sz];		  // Do not init, and do not use vector
	vector<double> maxlab(numk, 10 * 10); //THIS IS THE VARIABLE VALUE OF M, just start with 10

	double invxywt = 1.0 / (STEP * STEP); //NOTE: this is different from how usual SLIC/LKM works
	const int width = m_width;			  // Allow compiler to vectorize code

	for (int numitr = 0; numitr < NUMITR; numitr++)
	{

		vector<double> maxlab_old(maxlab);

#if _OPENMP
#pragma omp parallel for schedule(guided) reduction(vec_double_sum                                                                              \
													: sigmal, sigmaa, sigmab, sigmax, sigmay) reduction(vec_int_sum                             \
																										: clustersize) reduction(vec_double_max \
																																 : maxlab)
#endif
		for (int y = 0; y < m_height; y++)
		{
			int cnt = 0;
			cnt++;

			for (int x = 0; x < m_width; x++)
			{
				int i = y * m_width + x;
				distvec[i] = DBL_MAX;
			}
			for (int n = 0; n < numk; n++)
			{
				// Abort if out of range
				if (!((int)(kseedsy[n] - offset) <= y && y < (int)(kseedsy[n] + offset)))
				{
					continue;
				}

				const int x1 = max(0, (int)(kseedsx[n] - offset));
				const int x2 = min(m_width, (int)(kseedsx[n] + offset));
				const double inv_maxlab = 1 / maxlab_old[n];
				const double cons_kseedsl = kseedsl[n];
				const double cons_kseedsa = kseedsa[n];
				const double cons_kseedsb = kseedsb[n];
				const double cons_kseedsx = kseedsx[n];
				const double cons_y = (y - kseedsy[n]) * (y - kseedsy[n]);
				for (int x = x1; x < x2; x++)
				{
					int i = y * width + x;
					// _ASSERT(y < m_height && x < m_width && y >= 0 && x >= 0);

					double l = m_lvec[i];
					double a = m_avec[i];
					double b = m_bvec[i];

					distlab[i] = (l - cons_kseedsl) * (l - cons_kseedsl) +
								 (a - cons_kseedsa) * (a - cons_kseedsa) +
								 (b - cons_kseedsb) * (b - cons_kseedsb);
					double distxy = (x - cons_kseedsx) * (x - cons_kseedsx) + cons_y;

					//------------------------------------------------------------------------
					double dist = distlab[i] * inv_maxlab + distxy * invxywt; //only varying m, prettier superpixels
																			  //------------------------------------------------------------------------

					if (dist < distvec[i])
					{
						klabels[i] = n;
						distvec[i] = dist;
					}
				}
			}

			for (int x = 0; x < m_width; x++)
			{
				//-----------------------------------------------------------------
				// Assign the max color distance for a cluster
				//-----------------------------------------------------------------
				int i = y * width + x;
				int idx = klabels[i];
				if (numitr == 0 && cnt == 1)
				{
					maxlab[idx] = 1;
				}
				if (maxlab[idx] < distlab[i])
					maxlab[idx] = distlab[i];
				// distvec[i] = DBL_MAX;
				//-----------------------------------------------------------------
				// Recalculate the centroid and store in the seed values
				//-----------------------------------------------------------------

				//_ASSERT(klabels[j] >= 0);
				sigmal[idx] += m_lvec[i];
				sigmaa[idx] += m_avec[i];
				sigmab[idx] += m_bvec[i];
				sigmax[idx] += x;
				sigmay[idx] += y;

				clustersize[idx]++;
			}
		}
		for (int k = 0; k < numk; k++)
		{
			//_ASSERT(clustersize[k] > 0);
			// if (clustersize[k] <= 0)
			// 	clustersize[k] = 1;
			inv[k] = 1.0 / double(clustersize[k]); //computing inverse now to multiply, than divide later

			kseedsl[k] = sigmal[k] * inv[k];
			kseedsa[k] = sigmaa[k] * inv[k];
			kseedsb[k] = sigmab[k] * inv[k];
			kseedsx[k] = sigmax[k] * inv[k];
			kseedsy[k] = sigmay[k] * inv[k];

			// Reset
			sigmal[k] = 0;
			sigmaa[k] = 0;
			sigmab[k] = 0;
			sigmax[k] = 0;
			sigmay[k] = 0;
			clustersize[k] = 0;
		}
	}
}

//===========================================================================
///	SaveSuperpixelLabels2PGM
///
///	Save labels to PGM in raster scan order.
//===========================================================================
void SLIC::SaveSuperpixelLabels2PPM(
	char *filename,
	int *labels,
	const int width,
	const int height)
{
	FILE *fp;
	char header[20];

	fp = fopen(filename, "wb");

	// write the PPM header info, such as type, width, height and maximum
	fprintf(fp, "P6\n%d %d\n255\n", width, height);

	// write the RGB data
	unsigned char *rgb = new unsigned char[(width) * (height)*3];
	int k = 0;
	unsigned char c = 0;
	for (int i = 0; i < (height); i++)
	{
		for (int j = 0; j < (width); j++)
		{
			c = (unsigned char)(labels[k]);
			rgb[i * (width)*3 + j * 3 + 2] = labels[k] >> 16 & 0xff; // r
			rgb[i * (width)*3 + j * 3 + 1] = labels[k] >> 8 & 0xff;	 // g
			rgb[i * (width)*3 + j * 3 + 0] = labels[k] & 0xff;		 // b

			// rgb[i*(width) + j + 0] = c;
			k++;
		}
	}
	fwrite(rgb, width * height * 3, 1, fp);

	delete[] rgb;

	std::fclose(fp);
}

//===========================================================================
///	EnforceLabelConnectivity
///
///		1. finding an adjacent label for each new component at the start
///		2. if a certain component is too small, assigning the previously found
///		    adjacent label to this component, and not incrementing the label.
//===========================================================================
void SLIC::EnforceLabelConnectivity(
	int *labels, //input labels that need to be corrected to remove stray labels
	const int &width,
	const int &height,
	int *nlabels,	//new labels
	int &numlabels, //the number of labels changes in the end if segments are removed
	const int &K)	//the number of superpixels desired by the user
{
	//	const int dx8[8] = {-1, -1,  0,  1, 1, 1, 0, -1};
	//	const int dy8[8] = { 0, -1, -1, -1, 0, 1, 1,  1};

	const int dx4[4] = {-1, 0, 1, 0};
	const int dy4[4] = {0, -1, 0, 1};

	const int sz = width * height;
	const int SUPSZ = sz / K;
//nlabels.resize(sz, -1);
#pragma omp parallel for simd //schedule(static, 2048)
	for (int i = 0; i < sz; i++)
		nlabels[i] = -1;
	// int oindex(0);
	// int adjlabel(0); //adjacent label

	vector<area_info> seg_info;

	// BFS to tag new label, and gather info for mapping
	vector<omp_lock_t> lock_vec(numlabels);
	for (size_t i = 0; i < numlabels; i++)
	{
		omp_init_lock(&lock_vec[i]);
	}

	int local_label = 0;
	int label = 0;
	unordered_map<int, area_info *> seg_label_map;
	deque<pair<int, area_info *>> shrinked_area;
#if _OPENMP
#pragma omp parallel private(local_label)
#endif
	{
		int *xvec = new int[sz];
		int *yvec = new int[sz];
		const int thread_id = omp_get_thread_num();
		const int thread_num = omp_get_num_threads();
#if _OPENMP
#pragma omp for private(local_label)
#endif
		for (int j = 0; j < height; j++)
		{
			for (int k = 0; k < width; k++)
			{
				int seg_label = local_label * thread_num + thread_id;

				int oindex = j * width + k;
				if (nlabels[oindex] >= 0)
				{
					continue;
				}
				omp_set_lock(&lock_vec[labels[oindex]]);
				if (nlabels[oindex] >= 0)
				{
					omp_unset_lock(&lock_vec[labels[oindex]]);
					continue;
				}

				area_info info;
				info.index = oindex;
				info.x = k;
				info.y = j;
				info.seg_label = seg_label;
				info.new_label = 0;

				nlabels[oindex] = seg_label;
				//--------------------
				// Start a new segment
				//--------------------
				xvec[0] = k;
				yvec[0] = j;

				// BFS
				int count(1);
				for (int c = 0; c < count; c++)
				{
					for (int n = 0; n < 4; n++)
					{
						int x = xvec[c] + dx4[n];
						int y = yvec[c] + dy4[n];

						if ((x >= 0 && x < width) && (y >= 0 && y < height))
						{
							int nindex = y * width + x;

							if (nlabels[nindex] < 0 && labels[oindex] == labels[nindex])
							{
								xvec[count] = x;
								yvec[count] = y;
								if (info.index > nindex)
								{
									info.index = nindex;
									info.x = x;
									info.y = y;
								}

								nlabels[nindex] = seg_label;
								count++;
							}
						}
					}
				}
				info.count = count;
#pragma omp critical
				{
					seg_info.push_back(info);
				}
				omp_unset_lock(&lock_vec[labels[oindex]]);
				local_label++;
			}
		}

#pragma omp master
		{
			std::sort(seg_info.begin(), seg_info.end());

			for (auto &info : seg_info)
			{
				if (info.count <= SUPSZ >> 2)
				{
					// info.new_label = info.adjacent_index;
					shrinked_area.push_back(make_pair(info.seg_label, &info));
					continue;
				}
				info.new_label = label;
				seg_label_map[info.seg_label] = &info;
				label++;
			}

			while (!shrinked_area.empty())
			{

				auto pair = shrinked_area.front();
				if (pair.second->index == 0)
				{
					seg_label_map[pair.first] = pair.second;
					pair.second->new_label = 0;
					shrinked_area.pop_front();
					continue;
				}

				//-------------------------------------------------------
				// Quickly find an adjacent label for use later if needed
				//-------------------------------------------------------
				int adjacent_label = -1;
				for (int n = 0; n < 4; n++)
				{
					int x = pair.second->x + dx4[n];
					int y = pair.second->y + dy4[n];
					if ((x >= 0 && x < width) && (y >= 0 && y < height))
					{
						int nindex = y * width + x;
						if (nlabels[nindex] == pair.first)
						{
							continue;
						}

						if (seg_label_map.count(nlabels[nindex]) == 0)
						{
							continue;
							adjacent_label = -1;
							break;
						}
						else if (seg_label_map[nlabels[nindex]]->index < pair.second->index)
						{
							adjacent_label = nlabels[nindex];
						}
					}
				}
				if (adjacent_label == -1)
				{
					shrinked_area.push_back(pair);
				}
				else
				{
					seg_label_map[pair.first] = seg_label_map[adjacent_label];
				}
				shrinked_area.pop_front();
			}
		}

// Map old label to new label
#pragma omp barrier
#pragma omp for simd
		for (size_t i = 0; i < sz; i++)
		{
			labels[i] = seg_label_map[nlabels[i]]->new_label;
		}
	}

	numlabels = label;
}

//===========================================================================
///	PerformSLICO_ForGivenK
///
/// Zero parameter SLIC algorithm for a given number K of superpixels.
//===========================================================================
void SLIC::PerformSLICO_ForGivenK(
	const unsigned int *ubuff,
	const int width,
	const int height,
	int *klabels,
	int &numlabels,
	const int &K,	 //required number of superpixels
	const double &m) //weight given to spatial distance
{
	vector<double> kseedsl(0);
	vector<double> kseedsa(0);
	vector<double> kseedsb(0);
	vector<double> kseedsx(0);
	vector<double> kseedsy(0);

	//--------------------------------------------------
	m_width = width;
	m_height = height;
	int sz = m_width * m_height;
	//--------------------------------------------------

	//--------------------------------------------------
	// RGB2LAB
	// Init LUT
	for (size_t i = 0; i < 256; i++)
	{
		double tmp = i / 255.0;
		rgb_lut[i] = tmp / 12.92;
		rgb_pow_lut[i] = pow((tmp + 0.055) / 1.055, 2.4);
	}

	// Convert
	{
		auto startTime = Clock::now();
		DoRGBtoLABConversion(ubuff, m_lvec, m_avec, m_bvec);
		auto endTime = Clock::now();
		auto compTime = chrono::duration_cast<chrono::microseconds>(endTime - startTime);
		std::cout << "RGB2LAB Conversion time: " << (double)compTime.count() / 1000 << " ms" << endl;
	}

	//--------------------------------------------------

	bool perturbseeds(true);
	vector<double> edgemag(0);
	// if (perturbseeds)
	// {

	// 	auto startTime = Clock::now();
	// 	DetectLabEdges(m_lvec, m_avec, m_bvec, m_width, m_height, edgemag);
	// 	auto endTime = Clock::now();
	// 	auto compTime = chrono::duration_cast<chrono::microseconds>(endTime - startTime);
	// 	std::cout << "DetectLabEdges time: " << (double)compTime.count() / 1000 << " ms" << endl;
	// }
	auto startTime = Clock::now();
	GetLABXYSeeds_ForGivenK(kseedsl, kseedsa, kseedsb, kseedsx, kseedsy, K, perturbseeds, edgemag);
	auto endTime = Clock::now();
	auto compTime = chrono::duration_cast<chrono::microseconds>(endTime - startTime);
	std::cout << "GetLABXYSeeds time: " << (double)compTime.count() / 1000 << " ms" << endl;

	int STEP = sqrt(double(sz) / double(K)) + 2.0; //adding a small value in the even the STEP size is too small.
	startTime = Clock::now();
	PerformSuperpixelSegmentation_VariableSandM(kseedsl, kseedsa, kseedsb, kseedsx, kseedsy, klabels, STEP, 10);
	endTime = Clock::now();
	compTime = chrono::duration_cast<chrono::microseconds>(endTime - startTime);
	std::cout << "SuperpixelSegmentation time=" << compTime.count() / 1000 << " ms" << endl;
	numlabels = kseedsl.size();

	int *nlabels = new int[sz];
	startTime = Clock::now();
	EnforceLabelConnectivity(klabels, m_width, m_height, nlabels, numlabels, K);
	{
		// memcpy(klabels, nlabels, sz * sizeof(int));
	}
	endTime = Clock::now();
	compTime = chrono::duration_cast<chrono::microseconds>(endTime - startTime);
	std::cout << "EnforceLabelConnectivity time=" << compTime.count() / 1000 << " ms" << endl;
	if (nlabels)
		delete[] nlabels;
}
