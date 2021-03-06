/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup spview3d
 */

#include "DNA_camera_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "BLI_sys_types.h" /* int64_t */

#include "BLI_math_vector.h"

#include "BKE_camera.h"
#include "BKE_screen.h"

#include "GPU_matrix.h"

#include "ED_view3d.h" /* own include */

#define BL_ZERO_CLIP 0.001

/* Non Clipping Projection Functions
 * ********************************* */

/**
 * \note use #ED_view3d_ob_project_mat_get to get the projection matrix
 */
void ED_view3d_project_float_v2_m4(const ARegion *region,
                                   const float co[3],
                                   float r_co[2],
                                   float mat[4][4])
{
  float vec4[4];

  copy_v3_v3(vec4, co);
  vec4[3] = 1.0;
  /* r_co[0] = IS_CLIPPED; */ /* always overwritten */

  mul_m4_v4(mat, vec4);

  if (vec4[3] > FLT_EPSILON) {
    r_co[0] = (float)(region->winx / 2.0f) + (region->winx / 2.0f) * vec4[0] / vec4[3];
    r_co[1] = (float)(region->winy / 2.0f) + (region->winy / 2.0f) * vec4[1] / vec4[3];
  }
  else {
    zero_v2(r_co);
  }
}

/**
 * \note use #ED_view3d_ob_project_mat_get to get projecting mat
 */
void ED_view3d_project_float_v3_m4(const ARegion *region,
                                   const float co[3],
                                   float r_co[3],
                                   float mat[4][4])
{
  float vec4[4];

  copy_v3_v3(vec4, co);
  vec4[3] = 1.0;
  /* r_co[0] = IS_CLIPPED; */ /* always overwritten */

  mul_m4_v4(mat, vec4);

  if (vec4[3] > FLT_EPSILON) {
    r_co[0] = (float)(region->winx / 2.0f) + (region->winx / 2.0f) * vec4[0] / vec4[3];
    r_co[1] = (float)(region->winy / 2.0f) + (region->winy / 2.0f) * vec4[1] / vec4[3];
    r_co[2] = vec4[2] / vec4[3];
  }
  else {
    zero_v3(r_co);
  }
}

/* Clipping Projection Functions
 * ***************************** */

eV3DProjStatus ED_view3d_project_base(const struct ARegion *region, struct Base *base)
{
  eV3DProjStatus ret = ED_view3d_project_short_global(
      region, base->object->obmat[3], &base->sx, V3D_PROJ_TEST_CLIP_DEFAULT);

  if (ret != V3D_PROJ_RET_OK) {
    base->sx = IS_CLIPPED;
    base->sy = 0;
  }

  return ret;
}

/* perspmat is typically...
 * - 'rv3d->perspmat',   is_local == false
 * - 'rv3d->persmatob', is_local == true
 */
static eV3DProjStatus ed_view3d_project__internal(const ARegion *region,
                                                  const float perspmat[4][4],
                                                  const bool is_local, /* normally hidden */
                                                  const float co[3],
                                                  float r_co[2],
                                                  const eV3DProjTest flag)
{
  float vec4[4];

  /* check for bad flags */
  BLI_assert((flag & V3D_PROJ_TEST_ALL) == flag);

  if (flag & V3D_PROJ_TEST_CLIP_BB) {
    RegionView3D *rv3d = region->regiondata;
    if (rv3d->rflag & RV3D_CLIPPING) {
      if (ED_view3d_clipping_test(rv3d, co, is_local)) {
        return V3D_PROJ_RET_CLIP_BB;
      }
    }
  }

  copy_v3_v3(vec4, co);
  vec4[3] = 1.0;
  mul_m4_v4(perspmat, vec4);
  const float w = fabsf(vec4[3]);

  if ((flag & V3D_PROJ_TEST_CLIP_ZERO) && (w <= (float)BL_ZERO_CLIP)) {
    return V3D_PROJ_RET_CLIP_ZERO;
  }

  if ((flag & V3D_PROJ_TEST_CLIP_NEAR) && (vec4[2] <= -w)) {
    return V3D_PROJ_RET_CLIP_NEAR;
  }

  if ((flag & V3D_PROJ_TEST_CLIP_FAR) && (vec4[2] >= w)) {
    return V3D_PROJ_RET_CLIP_FAR;
  }

  const float scalar = (w != 0.0f) ? (1.0f / w) : 0.0f;
  const float fx = ((float)region->winx / 2.0f) * (1.0f + (vec4[0] * scalar));
  const float fy = ((float)region->winy / 2.0f) * (1.0f + (vec4[1] * scalar));

  if ((flag & V3D_PROJ_TEST_CLIP_WIN) &&
      (fx <= 0.0f || fy <= 0.0f || fx >= (float)region->winx || fy >= (float)region->winy)) {
    return V3D_PROJ_RET_CLIP_WIN;
  }

  r_co[0] = fx;
  r_co[1] = fy;

  return V3D_PROJ_RET_OK;
}

eV3DProjStatus ED_view3d_project_short_ex(const ARegion *region,
                                          float perspmat[4][4],
                                          const bool is_local,
                                          const float co[3],
                                          short r_co[2],
                                          const eV3DProjTest flag)
{
  float tvec[2];
  eV3DProjStatus ret = ed_view3d_project__internal(region, perspmat, is_local, co, tvec, flag);
  if (ret == V3D_PROJ_RET_OK) {
    if ((tvec[0] > -32700.0f && tvec[0] < 32700.0f) &&
        (tvec[1] > -32700.0f && tvec[1] < 32700.0f)) {
      r_co[0] = (short)floorf(tvec[0]);
      r_co[1] = (short)floorf(tvec[1]);
    }
    else {
      ret = V3D_PROJ_RET_OVERFLOW;
    }
  }
  return ret;
}

eV3DProjStatus ED_view3d_project_int_ex(const ARegion *region,
                                        float perspmat[4][4],
                                        const bool is_local,
                                        const float co[3],
                                        int r_co[2],
                                        const eV3DProjTest flag)
{
  float tvec[2];
  eV3DProjStatus ret = ed_view3d_project__internal(region, perspmat, is_local, co, tvec, flag);
  if (ret == V3D_PROJ_RET_OK) {
    if ((tvec[0] > -2140000000.0f && tvec[0] < 2140000000.0f) &&
        (tvec[1] > -2140000000.0f && tvec[1] < 2140000000.0f)) {
      r_co[0] = (int)floorf(tvec[0]);
      r_co[1] = (int)floorf(tvec[1]);
    }
    else {
      ret = V3D_PROJ_RET_OVERFLOW;
    }
  }
  return ret;
}

eV3DProjStatus ED_view3d_project_float_ex(const ARegion *region,
                                          float perspmat[4][4],
                                          const bool is_local,
                                          const float co[3],
                                          float r_co[2],
                                          const eV3DProjTest flag)
{
  float tvec[2];
  eV3DProjStatus ret = ed_view3d_project__internal(region, perspmat, is_local, co, tvec, flag);
  if (ret == V3D_PROJ_RET_OK) {
    if (isfinite(tvec[0]) && isfinite(tvec[1])) {
      copy_v2_v2(r_co, tvec);
    }
    else {
      ret = V3D_PROJ_RET_OVERFLOW;
    }
  }
  return ret;
}

/* --- short --- */
eV3DProjStatus ED_view3d_project_short_global(const ARegion *region,
                                              const float co[3],
                                              short r_co[2],
                                              const eV3DProjTest flag)
{
  RegionView3D *rv3d = region->regiondata;
  return ED_view3d_project_short_ex(region, rv3d->persmat, false, co, r_co, flag);
}
/* object space, use ED_view3d_init_mats_rv3d before calling */
eV3DProjStatus ED_view3d_project_short_object(const ARegion *region,
                                              const float co[3],
                                              short r_co[2],
                                              const eV3DProjTest flag)
{
  RegionView3D *rv3d = region->regiondata;
  ED_view3d_check_mats_rv3d(rv3d);
  return ED_view3d_project_short_ex(region, rv3d->persmatob, true, co, r_co, flag);
}

/* --- int --- */
eV3DProjStatus ED_view3d_project_int_global(const ARegion *region,
                                            const float co[3],
                                            int r_co[2],
                                            const eV3DProjTest flag)
{
  RegionView3D *rv3d = region->regiondata;
  return ED_view3d_project_int_ex(region, rv3d->persmat, false, co, r_co, flag);
}
/* object space, use ED_view3d_init_mats_rv3d before calling */
eV3DProjStatus ED_view3d_project_int_object(const ARegion *region,
                                            const float co[3],
                                            int r_co[2],
                                            const eV3DProjTest flag)
{
  RegionView3D *rv3d = region->regiondata;
  ED_view3d_check_mats_rv3d(rv3d);
  return ED_view3d_project_int_ex(region, rv3d->persmatob, true, co, r_co, flag);
}

/* --- float --- */
eV3DProjStatus ED_view3d_project_float_global(const ARegion *region,
                                              const float co[3],
                                              float r_co[2],
                                              const eV3DProjTest flag)
{
  RegionView3D *rv3d = region->regiondata;
  return ED_view3d_project_float_ex(region, rv3d->persmat, false, co, r_co, flag);
}
/* object space, use ED_view3d_init_mats_rv3d before calling */
eV3DProjStatus ED_view3d_project_float_object(const ARegion *region,
                                              const float co[3],
                                              float r_co[2],
                                              const eV3DProjTest flag)
{
  RegionView3D *rv3d = region->regiondata;
  ED_view3d_check_mats_rv3d(rv3d);
  return ED_view3d_project_float_ex(region, rv3d->persmatob, true, co, r_co, flag);
}

/* More Generic Window/Ray/Vector projection functions
 * *************************************************** */

float ED_view3d_pixel_size(const RegionView3D *rv3d, const float co[3])
{
  return mul_project_m4_v3_zfac(rv3d->persmat, co) * rv3d->pixsize * U.pixelsize;
}

float ED_view3d_pixel_size_no_ui_scale(const RegionView3D *rv3d, const float co[3])
{
  return mul_project_m4_v3_zfac(rv3d->persmat, co) * rv3d->pixsize;
}

/**
 * Calculate a depth value from \a co, use with #ED_view3d_win_to_delta
 */
float ED_view3d_calc_zfac(const RegionView3D *rv3d, const float co[3], bool *r_flip)
{
  float zfac = mul_project_m4_v3_zfac(rv3d->persmat, co);

  if (r_flip) {
    *r_flip = (zfac < 0.0f);
  }

  /* if x,y,z is exactly the viewport offset, zfac is 0 and we don't want that
   * (accounting for near zero values) */
  if (zfac < 1.e-6f && zfac > -1.e-6f) {
    zfac = 1.0f;
  }

  /* Negative zfac means x, y, z was behind the camera (in perspective).
   * This gives flipped directions, so revert back to ok default case. */
  if (zfac < 0.0f) {
    zfac = -zfac;
  }

  return zfac;
}

/**
 * Calculate a depth value from `co` (result should only be used for comparison).
 */
float ED_view3d_calc_depth_for_comparison(const RegionView3D *rv3d, const float co[3])
{
  if (rv3d->is_persp) {
    return ED_view3d_calc_zfac(rv3d, co, NULL);
  }
  return -dot_v3v3(rv3d->viewinv[2], co);
}

static void view3d_win_to_ray_segment(struct Depsgraph *depsgraph,
                                      const ARegion *region,
                                      const View3D *v3d,
                                      const float mval[2],
                                      float r_ray_co[3],
                                      float r_ray_dir[3],
                                      float r_ray_start[3],
                                      float r_ray_end[3])
{
  RegionView3D *rv3d = region->regiondata;
  float _ray_co[3], _ray_dir[3], start_offset, end_offset;

  if (!r_ray_co) {
    r_ray_co = _ray_co;
  }
  if (!r_ray_dir) {
    r_ray_dir = _ray_dir;
  }

  ED_view3d_win_to_origin(region, mval, r_ray_co);
  ED_view3d_win_to_vector(region, mval, r_ray_dir);

  if ((rv3d->is_persp == false) && (rv3d->persp != RV3D_CAMOB)) {
    end_offset = v3d->clip_end / 2.0f;
    start_offset = -end_offset;
  }
  else {
    ED_view3d_clip_range_get(depsgraph, v3d, rv3d, &start_offset, &end_offset, false);
  }

  if (r_ray_start) {
    madd_v3_v3v3fl(r_ray_start, r_ray_co, r_ray_dir, start_offset);
  }
  if (r_ray_end) {
    madd_v3_v3v3fl(r_ray_end, r_ray_co, r_ray_dir, end_offset);
  }
}

bool ED_view3d_clip_segment(const RegionView3D *rv3d, float ray_start[3], float ray_end[3])
{
  if ((rv3d->rflag & RV3D_CLIPPING) &&
      (clip_segment_v3_plane_n(ray_start, ray_end, rv3d->clip, 6, ray_start, ray_end) == false)) {
    return false;
  }
  return true;
}

/**
 * Calculate a 3d viewpoint and direction vector from 2d window coordinates.
 * This ray_start is located at the viewpoint, ray_normal is the direction towards mval.
 * ray_start is clipped by the view near limit so points in front of it are always in view.
 * In orthographic view the resulting ray_normal will match the view vector.
 * This version also returns the ray_co point of the ray on window plane, useful to fix precision
 * issues esp. with ortho view, where default ray_start is set rather far away.
 * \param region: The region (used for the window width and height).
 * \param v3d: The 3d viewport (used for near clipping value).
 * \param mval: The area relative 2d location (such as event->mval, converted into float[2]).
 * \param r_ray_co: The world-space point where the ray intersects the window plane.
 * \param r_ray_normal: The normalized world-space direction of towards mval.
 * \param r_ray_start: The world-space starting point of the ray.
 * \param do_clip_planes: Optionally clip the start of the ray by the view clipping planes.
 * \return success, false if the ray is totally clipped.
 */
bool ED_view3d_win_to_ray_clipped_ex(struct Depsgraph *depsgraph,
                                     const ARegion *region,
                                     const View3D *v3d,
                                     const float mval[2],
                                     float r_ray_co[3],
                                     float r_ray_normal[3],
                                     float r_ray_start[3],
                                     bool do_clip_planes)
{
  float ray_end[3];

  view3d_win_to_ray_segment(
      depsgraph, region, v3d, mval, r_ray_co, r_ray_normal, r_ray_start, ray_end);

  /* bounds clipping */
  if (do_clip_planes) {
    return ED_view3d_clip_segment(region->regiondata, r_ray_start, ray_end);
  }

  return true;
}

/**
 * Calculate a 3d viewpoint and direction vector from 2d window coordinates.
 * This ray_start is located at the viewpoint, ray_normal is the direction towards mval.
 * ray_start is clipped by the view near limit so points in front of it are always in view.
 * In orthographic view the resulting ray_normal will match the view vector.
 * \param region: The region (used for the window width and height).
 * \param v3d: The 3d viewport (used for near clipping value).
 * \param mval: The area relative 2d location (such as event->mval, converted into float[2]).
 * \param r_ray_start: The world-space point where the ray intersects the window plane.
 * \param r_ray_normal: The normalized world-space direction of towards mval.
 * \param do_clip_planes: Optionally clip the start of the ray by the view clipping planes.
 * \return success, false if the ray is totally clipped.
 */
bool ED_view3d_win_to_ray_clipped(struct Depsgraph *depsgraph,
                                  const ARegion *region,
                                  const View3D *v3d,
                                  const float mval[2],
                                  float r_ray_start[3],
                                  float r_ray_normal[3],
                                  const bool do_clip_planes)
{
  return ED_view3d_win_to_ray_clipped_ex(
      depsgraph, region, v3d, mval, NULL, r_ray_normal, r_ray_start, do_clip_planes);
}

/**
 * Calculate a 3d viewpoint and direction vector from 2d window coordinates.
 * This ray_start is located at the viewpoint, ray_normal is the direction towards mval.
 * \param region: The region (used for the window width and height).
 * \param mval: The area relative 2d location (such as event->mval, converted into float[2]).
 * \param r_ray_start: The world-space point where the ray intersects the window plane.
 * \param r_ray_normal: The normalized world-space direction of towards mval.
 *
 * \note Ignores view near/far clipping,
 * to take this into account use #ED_view3d_win_to_ray_clipped.
 */
void ED_view3d_win_to_ray(const ARegion *region,
                          const float mval[2],
                          float r_ray_start[3],
                          float r_ray_normal[3])
{
  ED_view3d_win_to_origin(region, mval, r_ray_start);
  ED_view3d_win_to_vector(region, mval, r_ray_normal);
}

/**
 * Calculate a normalized 3d direction vector from the viewpoint towards a global location.
 * In orthographic view the resulting vector will match the view vector.
 * \param rv3d: The region (used for the window width and height).
 * \param coord: The world-space location.
 * \param vec: The resulting normalized vector.
 */
void ED_view3d_global_to_vector(const RegionView3D *rv3d, const float coord[3], float vec[3])
{
  if (rv3d->is_persp) {
    float p1[4], p2[4];

    copy_v3_v3(p1, coord);
    p1[3] = 1.0f;
    copy_v3_v3(p2, p1);
    p2[3] = 1.0f;
    mul_m4_v4(rv3d->viewmat, p2);

    mul_v3_fl(p2, 2.0f);

    mul_m4_v4(rv3d->viewinv, p2);

    sub_v3_v3v3(vec, p1, p2);
  }
  else {
    copy_v3_v3(vec, rv3d->viewinv[2]);
  }
  normalize_v3(vec);
}

/* very similar to ED_view3d_win_to_3d() but has no advantage, de-duplicating */
#if 0
bool view3d_get_view_aligned_coordinate(ARegion *region,
                                        float fp[3],
                                        const int mval[2],
                                        const bool do_fallback)
{
  RegionView3D *rv3d = region->regiondata;
  float dvec[3];
  int mval_cpy[2];
  eV3DProjStatus ret;

  ret = ED_view3d_project_int_global(region, fp, mval_cpy, V3D_PROJ_TEST_NOP);

  if (ret == V3D_PROJ_RET_OK) {
    const float mval_f[2] = {(float)(mval_cpy[0] - mval[0]), (float)(mval_cpy[1] - mval[1])};
    const float zfac = ED_view3d_calc_zfac(rv3d, fp, NULL);
    ED_view3d_win_to_delta(region, mval_f, dvec, zfac);
    sub_v3_v3(fp, dvec);

    return true;
  }
  else {
    /* fallback to the view center */
    if (do_fallback) {
      negate_v3_v3(fp, rv3d->ofs);
      return view3d_get_view_aligned_coordinate(region, fp, mval, false);
    }
    else {
      return false;
    }
  }
}
#endif

/**
 * Calculate a 3d location from 2d window coordinates.
 * \param region: The region (used for the window width and height).
 * \param depth_pt: The reference location used to calculate the Z depth.
 * \param mval: The area relative location (such as event->mval converted to floats).
 * \param r_out: The resulting world-space location.
 */
void ED_view3d_win_to_3d(const View3D *v3d,
                         const ARegion *region,
                         const float depth_pt[3],
                         const float mval[2],
                         float r_out[3])
{
  RegionView3D *rv3d = region->regiondata;

  float ray_origin[3];
  float ray_direction[3];
  float lambda;

  if (rv3d->is_persp) {
    float plane[4];

    copy_v3_v3(ray_origin, rv3d->viewinv[3]);
    ED_view3d_win_to_vector(region, mval, ray_direction);

    /* NOTE: we could use #isect_line_plane_v3()
     * however we want the intersection to be in front of the view no matter what,
     * so apply the unsigned factor instead. */
    plane_from_point_normal_v3(plane, depth_pt, rv3d->viewinv[2]);

    isect_ray_plane_v3(ray_origin, ray_direction, plane, &lambda, false);
    lambda = fabsf(lambda);
  }
  else {
    float dx = (2.0f * mval[0] / (float)region->winx) - 1.0f;
    float dy = (2.0f * mval[1] / (float)region->winy) - 1.0f;

    if (rv3d->persp == RV3D_CAMOB) {
      /* ortho camera needs offset applied */
      const Camera *cam = v3d->camera->data;
      const int sensor_fit = BKE_camera_sensor_fit(cam->sensor_fit, region->winx, region->winy);
      const float zoomfac = BKE_screen_view3d_zoom_to_fac(rv3d->camzoom) * 4.0f;
      const float aspx = region->winx / (float)region->winy;
      const float aspy = region->winy / (float)region->winx;
      const float shiftx = cam->shiftx * 0.5f *
                           (sensor_fit == CAMERA_SENSOR_FIT_HOR ? 1.0f : aspy);
      const float shifty = cam->shifty * 0.5f *
                           (sensor_fit == CAMERA_SENSOR_FIT_HOR ? aspx : 1.0f);

      dx += (rv3d->camdx + shiftx) * zoomfac;
      dy += (rv3d->camdy + shifty) * zoomfac;
    }
    ray_origin[0] = (rv3d->persinv[0][0] * dx) + (rv3d->persinv[1][0] * dy) + rv3d->viewinv[3][0];
    ray_origin[1] = (rv3d->persinv[0][1] * dx) + (rv3d->persinv[1][1] * dy) + rv3d->viewinv[3][1];
    ray_origin[2] = (rv3d->persinv[0][2] * dx) + (rv3d->persinv[1][2] * dy) + rv3d->viewinv[3][2];

    copy_v3_v3(ray_direction, rv3d->viewinv[2]);
    lambda = ray_point_factor_v3(depth_pt, ray_origin, ray_direction);
  }

  madd_v3_v3v3fl(r_out, ray_origin, ray_direction, lambda);
}

void ED_view3d_win_to_3d_int(const View3D *v3d,
                             const ARegion *region,
                             const float depth_pt[3],
                             const int mval[2],
                             float r_out[3])
{
  const float mval_fl[2] = {mval[0], mval[1]};
  ED_view3d_win_to_3d(v3d, region, depth_pt, mval_fl, r_out);
}

bool ED_view3d_win_to_3d_on_plane(const ARegion *region,
                                  const float plane[4],
                                  const float mval[2],
                                  const bool do_clip,
                                  float r_out[3])
{
  float ray_co[3], ray_no[3];
  ED_view3d_win_to_origin(region, mval, ray_co);
  ED_view3d_win_to_vector(region, mval, ray_no);
  float lambda;
  if (isect_ray_plane_v3(ray_co, ray_no, plane, &lambda, do_clip)) {
    madd_v3_v3v3fl(r_out, ray_co, ray_no, lambda);
    return true;
  }
  return false;
}

bool ED_view3d_win_to_3d_on_plane_int(const ARegion *region,
                                      const float plane[4],
                                      const int mval[2],
                                      const bool do_clip,
                                      float r_out[3])
{
  const float mval_fl[2] = {mval[0], mval[1]};
  return ED_view3d_win_to_3d_on_plane(region, plane, mval_fl, do_clip, r_out);
}

/**
 * A wrapper for #ED_view3d_win_to_3d_on_plane that projects onto \a plane_fallback
 * then maps this back to \a plane.
 *
 * This is intended to be used when \a plane is orthogonal to the views Z axis where
 * projecting the \a mval doesn't work well (or fail completely when exactly aligned).
 */
bool ED_view3d_win_to_3d_on_plane_with_fallback(const ARegion *region,
                                                const float plane[4],
                                                const float mval[2],
                                                const bool do_clip,
                                                const float plane_fallback[4],
                                                float r_out[3])
{
  float isect_co[3], isect_no[3];
  if (!isect_plane_plane_v3(plane, plane_fallback, isect_co, isect_no)) {
    return false;
  }
  normalize_v3(isect_no);

  /* Construct matrix to transform `plane_fallback` onto `plane`. */
  float mat4[4][4];
  {
    float mat3[3][3];
    rotation_between_vecs_to_mat3(mat3, plane, plane_fallback);
    copy_m4_m3(mat4, mat3);
    transform_pivot_set_m4(mat4, isect_co);
  }

  float co[3];
  if (!ED_view3d_win_to_3d_on_plane(region, plane_fallback, mval, do_clip, co)) {
    return false;
  }
  mul_m4_v3(mat4, co);

  /* While the point is already on the plane, there may be some small in-precision
   * so ensure the point is exactly on the plane. */
  closest_to_plane_v3(r_out, plane, co);

  return true;
}

/**
 * Calculate a 3d difference vector from 2d window offset.
 * note that #ED_view3d_calc_zfac() must be called first to determine
 * the depth used to calculate the delta.
 * \param region: The region (used for the window width and height).
 * \param mval: The area relative 2d difference (such as event->mval[0] - other_x).
 * \param out: The resulting world-space delta.
 */
void ED_view3d_win_to_delta(const ARegion *region,
                            const float mval[2],
                            float out[3],
                            const float zfac)
{
  RegionView3D *rv3d = region->regiondata;
  float dx, dy;

  dx = 2.0f * mval[0] * zfac / region->winx;
  dy = 2.0f * mval[1] * zfac / region->winy;

  out[0] = (rv3d->persinv[0][0] * dx + rv3d->persinv[1][0] * dy);
  out[1] = (rv3d->persinv[0][1] * dx + rv3d->persinv[1][1] * dy);
  out[2] = (rv3d->persinv[0][2] * dx + rv3d->persinv[1][2] * dy);
}

/**
 * Calculate a 3d origin from 2d window coordinates.
 * \note Orthographic views have a less obvious origin,
 * Since far clip can be a very large value resulting in numeric precision issues,
 * the origin in this case is close to zero coordinate.
 *
 * \param region: The region (used for the window width and height).
 * \param mval: The area relative 2d location (such as event->mval converted to floats).
 * \param out: The resulting normalized world-space direction vector.
 */
void ED_view3d_win_to_origin(const ARegion *region, const float mval[2], float out[3])
{
  RegionView3D *rv3d = region->regiondata;
  if (rv3d->is_persp) {
    copy_v3_v3(out, rv3d->viewinv[3]);
  }
  else {
    out[0] = 2.0f * mval[0] / region->winx - 1.0f;
    out[1] = 2.0f * mval[1] / region->winy - 1.0f;

    if (rv3d->persp == RV3D_CAMOB) {
      out[2] = -1.0f;
    }
    else {
      out[2] = 0.0f;
    }

    mul_project_m4_v3(rv3d->persinv, out);
  }
}

/**
 * Calculate a 3d direction vector from 2d window coordinates.
 * This direction vector starts and the view in the direction of the 2d window coordinates.
 * In orthographic view all window coordinates yield the same vector.
 *
 * \note doesn't rely on ED_view3d_calc_zfac
 * for perspective view, get the vector direction to
 * the mouse cursor as a normalized vector.
 *
 * \param region: The region (used for the window width and height).
 * \param mval: The area relative 2d location (such as event->mval converted to floats).
 * \param out: The resulting normalized world-space direction vector.
 */
void ED_view3d_win_to_vector(const ARegion *region, const float mval[2], float out[3])
{
  RegionView3D *rv3d = region->regiondata;

  if (rv3d->is_persp) {
    out[0] = 2.0f * (mval[0] / region->winx) - 1.0f;
    out[1] = 2.0f * (mval[1] / region->winy) - 1.0f;
    out[2] = -0.5f;
    mul_project_m4_v3(rv3d->persinv, out);
    sub_v3_v3(out, rv3d->viewinv[3]);
  }
  else {
    negate_v3_v3(out, rv3d->viewinv[2]);
  }
  normalize_v3(out);
}

/**
 * Calculate a 3d segment from 2d window coordinates.
 * This ray_start is located at the viewpoint, ray_end is a far point.
 * ray_start and ray_end are clipped by the view near and far limits
 * so points along this line are always in view.
 * In orthographic view all resulting segments will be parallel.
 * \param region: The region (used for the window width and height).
 * \param v3d: The 3d viewport (used for near and far clipping range).
 * \param mval: The area relative 2d location (such as event->mval, converted into float[2]).
 * \param r_ray_start: The world-space starting point of the segment.
 * \param r_ray_end: The world-space end point of the segment.
 * \param do_clip_planes: Optionally clip the ray by the view clipping planes.
 * \return success, false if the segment is totally clipped.
 */
bool ED_view3d_win_to_segment_clipped(struct Depsgraph *depsgraph,
                                      const ARegion *region,
                                      View3D *v3d,
                                      const float mval[2],
                                      float r_ray_start[3],
                                      float r_ray_end[3],
                                      const bool do_clip_planes)
{
  view3d_win_to_ray_segment(depsgraph, region, v3d, mval, NULL, NULL, r_ray_start, r_ray_end);

  /* bounds clipping */
  if (do_clip_planes) {
    return ED_view3d_clip_segment((RegionView3D *)region->regiondata, r_ray_start, r_ray_end);
  }

  return true;
}

/* -------------------------------------------------------------------- */
/** \name Utility functions for projection
 * \{ */

void ED_view3d_ob_project_mat_get(const RegionView3D *rv3d, const Object *ob, float r_pmat[4][4])
{
  float vmat[4][4];

  mul_m4_m4m4(vmat, rv3d->viewmat, ob->obmat);
  mul_m4_m4m4(r_pmat, rv3d->winmat, vmat);
}

void ED_view3d_ob_project_mat_get_from_obmat(const RegionView3D *rv3d,
                                             const float obmat[4][4],
                                             float r_pmat[4][4])
{
  float vmat[4][4];

  mul_m4_m4m4(vmat, rv3d->viewmat, obmat);
  mul_m4_m4m4(r_pmat, rv3d->winmat, vmat);
}

/**
 * Convert between region relative coordinates (x,y) and depth component z and
 * a point in world space. */
void ED_view3d_project_v3(const struct ARegion *region, const float world[3], float r_region_co[3])
{
  /* Viewport is set up to make coordinates relative to the region, not window. */
  RegionView3D *rv3d = region->regiondata;
  const int viewport[4] = {0, 0, region->winx, region->winy};
  GPU_matrix_project_3fv(world, rv3d->viewmat, rv3d->winmat, viewport, r_region_co);
}

void ED_view3d_project_v2(const struct ARegion *region, const float world[3], float r_region_co[2])
{
  /* Viewport is set up to make coordinates relative to the region, not window. */
  RegionView3D *rv3d = region->regiondata;
  const int viewport[4] = {0, 0, region->winx, region->winy};
  GPU_matrix_project_2fv(world, rv3d->viewmat, rv3d->winmat, viewport, r_region_co);
}

bool ED_view3d_unproject_v3(
    const struct ARegion *region, float regionx, float regiony, float regionz, float world[3])
{
  RegionView3D *rv3d = region->regiondata;
  const int viewport[4] = {0, 0, region->winx, region->winy};
  const float region_co[3] = {regionx, regiony, regionz};

  return GPU_matrix_unproject_3fv(region_co, rv3d->viewinv, rv3d->winmat, viewport, world);
}

/** \} */
