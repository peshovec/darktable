#include "levmar-2.6/levmar.h"
#include <stdio.h>
#include <stdlib.h>

// some internal details of the modules we're trying to optimize.
// these are reproduced here because we want to stick to the specific version unless
// the rest of this code is updated, too.

// tonecurve
// ======================================================================
static const int tonecurve_version = 4;

typedef struct dt_iop_tonecurve_node_t
{
  float x;
  float y;
}
dt_iop_tonecurve_node_t;

typedef struct dt_iop_tonecurve_params_t
{
  dt_iop_tonecurve_node_t tonecurve[3][DT_IOP_TONECURVE_MAXNODES];  // three curves (L, a, b) with max number of nodes
  int tonecurve_nodes[3];
  int tonecurve_type[3];
  int tonecurve_autoscale_ab;
  int tonecurve_preset;
  int tonecurve_unbound_ab;
}
dt_iop_tonecurve_params_t;

// color correction
// ======================================================================
static const int colorcorrection_version = 1;

typedef struct dt_iop_colorcorrection_params_t
{
  float hia, hib, loa, lob, saturation;
}
dt_iop_colorcorrection_params_t;


// color zones
// ======================================================================
static const int colorzones_version = 3;
#define DT_IOP_COLORZONES_BANDS 8

typedef enum dt_iop_colorzones_channel_t
{
  DT_IOP_COLORZONES_L = 0,
  DT_IOP_COLORZONES_C = 1,
  DT_IOP_COLORZONES_h = 2
}
dt_iop_colorzones_channel_t;

typedef struct dt_iop_colorzones_params_t
{
  int32_t channel;
  float equalizer_x[3][DT_IOP_COLORZONES_BANDS], equalizer_y[3][DT_IOP_COLORZONES_BANDS];
  float strength;
}
dt_iop_colorzones_params_t;

// monochrome
// ======================================================================
static const int monochrome_version = 2;

typedef struct dt_iop_monochrome_params_t
{
  float a, b, size, highlights;
}
dt_iop_monochrome_params_t;

// ======================================================================

typedef struct module_params_t
{
  dt_iop_tonecurve_params_t curve;
  dt_iop_colorcorrection_params_t corr;
  dt_iop_colorzones_params_t zones;
  dt_iop_monochrome_params_t mono;
}
module_params_t;

static inline module_params_t *init_params()
{
  module_params_t *m = (module_params_t *)malloc(sizeof(module_params_t));
  memset(m, 0, sizeof(module_params_t));

  // curve:
  for(int k=0;k<3;k++) m->curve.tonecurve_type[k] = 2; // MONOTONE_HERMITE
  for(int k=0;k<3;k++) m->curve.tonecurve_nodes[k] = 9; // enough i think.
  for(int i=0;i<3;i++)
    for(int k=0;k<9;k++)
      m->curve.tonecurve[i][k] = k/8.0f; // start at identity
  m->curve.tonecurve_autoscale_ab = 0;
  m->curve.tonecurve_preset = 0;
  m->curve.tonecurve_unbound_ab = 0;

  // color correction
  m->corr.saturation = 1.0f; // the rest is 0

  // color zones
  for(int ch=0; ch<3; ch++)
    for(int k=0; k<DT_IOP_COLORZONES_BANDS; k++)
      m->zones.equalizer_y[ch][k] = 0.5f;
  m->zones.strength = 0.0;
  m->zones.channel = DT_IOP_COLORZONES_h;

  // monochrome:
  m->mono.a = 0.f;
  m->mono.b = 0.f;
  m->mono.size = 2.f;
  m->mono.highlights= 0.f;

  return m;
}

static inline int params2float(const module_params_t *m, float *f)
{
  int j = 0;
  for(int i=0;i<3;i++)
    for(int k=0;k<9;k++)
      f[j++] = m->curve.tonecurve[i][k];

  f[j++] = m->corr.hia;
  f[j++] = m->corr.hib;
  f[j++] = m->corr.loa;
  f[j++] = m->corr.lob;
  f[j++] = m->corr.saturation;

  for(int ch=0; ch<3; ch++)
    for(int k=0; k<DT_IOP_COLORZONES_BANDS; k++)
      f[j++] = m->zones.equalizer_x[ch][k];
  f[j++] = m->zones.strength;
  // TODO: shall we mutate this? probably not.
  // f[j++] = m->zones.channel+.5f;

  f[j++] = m->mono.a;
  f[j++] = m->mono.b;
  f[j++] = m->mono.size;
  f[j++] = m->mono.highlights;

  return j;
}

static inline int float2params(const float *f, module_params_t *m)
{
  int j = 0;
  for(int i=0;i<3;i++)
    for(int k=0;k<9;k++)
      m->curve.tonecurve[i][k] = f[j++];

  m->corr.hia = f[j++];
  m->corr.hib = f[j++];
  m->corr.loa = f[j++];
  m->corr.lob = f[j++];
  m->corr.saturation = f[j++];

  for(int ch=0; ch<3; ch++)
    for(int k=0; k<DT_IOP_COLORZONES_BANDS; k++)
      m->zones.equalizer_x[ch][k] = f[j++];
  m->zones.strength = f[j++];
  // TODO: shall we mutate this? probably not.
  // m->zones.channel = (int)roundf(f[j++]);

  m->mono.a = f[j++];
  m->mono.b = f[j++];
  m->mono.size = f[j++];
  m->mono.highlights = f[j++];

  return j;
}

void eval_diff(float *param, float *sample, int param_cnt, int sample_cnt, void *data)
{
  // now the nasty part.
}

int main(int argc, char *argv[])
{
  // TODO: load lut or image processed with that?
  // TODO: construct param vector based on a couple of modules:
  //  - color correction
  //  - base curve or tone curve?
  //  - monochrome?
  //  - color zones

  float opts[LM_OPTS_SZ], info[LM_INFO_SZ];
  // opts[0]=LM_INIT_MU; opts[1]=1E-3; opts[2]=1E-5; opts[3]=1E-7; // terminates way to early
  // opts[0]=LM_INIT_MU; opts[1]=1E-7; opts[2]=1E-7; opts[3]=1E-12; // known to go through
  opts[0]=1e-2f; opts[1]=1E-7; opts[2]=1E-7; opts[3]=1E-12; // known to go through
  // opts[0]=LM_INIT_MU; opts[1]=1E-7; opts[2]=1E-8; opts[3]=1E-13; // goes through, some nans
  // opts[0]=LM_INIT_MU; opts[1]=1E-8; opts[2]=1E-8; opts[3]=1E-15;
  opts[4]= LM_DIFF_DELTA;
  slevmar_dif(eval_diff, param, sample, param_cnt, sample_cnt, 1000, opts, info, NULL, NULL, &tmp);
  exit(0);
}
