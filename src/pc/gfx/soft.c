/*
 * software gfx transformations
 */
#include "soft.h"
#include "pcgfx.h"
#include "tex.h"
#include "math.h"

#define gte_divide(a,b) ((a)/(b))

sw_transform_struct params;

extern entry *cur_zone;

static rgb Rgb8To32(rgb8 c) {
  rgb res;
  res.r = c.r*16843009;
  res.g = c.g*16843009;
  res.b = c.b*16843009;
  return res;
}

static rgba Rgb8ToA32(rgb8 c) {
  rgba res;
  res.r = c.r*16843009;
  res.g = c.g*16843009;
  res.b = c.b*16843009;
  res.a = ~0;
  return res;
}

uint32_t SwSqrMagnitude2(int32_t a, int32_t b) {
  return ((int64_t)a*a+b*b) >> 13;
}

uint32_t SwSqrMagnitude3(int32_t a, int32_t b, int32_t c) {
  return ((int64_t)a*a+b*b+c*c) >> 20;
}

int SwRot(vec *in, vec *out, mat16 *m_rot) {
  vec r_vert;
  int res;

#define v in
#define r m_rot->m
  r_vert.x=(int64_t)((int64_t)(r[0][0]*v->x)+(r[0][1]*v->y)+(r[0][2]*v->z))>>12;
  r_vert.y=(int64_t)((int64_t)(r[1][0]*v->x)+(r[1][1]*v->y)+(r[1][2]*v->z))>>12;
  r_vert.z=(int64_t)((int64_t)(r[2][0]*v->x)+(r[2][1]*v->y)+(r[2][2]*v->z))>>12;
#undef v
#undef r
  res = 1;
  if (r_vert.x > 0x7FFF || r_vert.x < -0x8000
   || r_vert.y > 0x7FFF || r_vert.y < -0x8000
   || r_vert.z > 0xFFFF || r_vert.z < 0)
    res = 0;
  *out=r_vert;
  return res;
}

int SwRotTrans(vec *in, vec *out, vec *trans, mat16 *m_rot) {
  vec r_vert;
  int res;

#define v in
#define t trans
#define r m_rot->m
  r_vert.x=(int64_t)((int64_t)(t->x<<12)+(r[0][0]*v->x)+(r[0][1]*v->y)+(r[0][2]*v->z))>>12;
  r_vert.y=(int64_t)((int64_t)(t->y<<12)+(r[1][0]*v->x)+(r[1][1]*v->y)+(r[1][2]*v->z))>>12;
  r_vert.z=(int64_t)((int64_t)(t->z<<12)+(r[2][0]*v->x)+(r[2][1]*v->y)+(r[2][2]*v->z))>>12;
#undef v
#undef t
#undef r
  res = 1;
  if (r_vert.x > 0x7FFF || r_vert.x < -0x8000
   || r_vert.y > 0x7FFF || r_vert.y < -0x8000
   || r_vert.z > 0xFFFF || r_vert.z < 0)
    res = 0;
  *out=r_vert;
  return res;
}

/* accuracy here depends on whether GTE division is emulated or normal division is used */
int SwRotTransPers(vec *in, vec *out, vec *trans, mat16 *m_rot, vec2 *offs, uint32_t proj) {
  vec r_vert;
  vec s_vert;
  int res;

  res = SwRotTrans(in, &r_vert, trans, m_rot);
  if (r_vert.z == 0) { r_vert.z = 1; }
  s_vert.x = ((int64_t)(offs->x<<16)+(((int64_t)r_vert.x*(proj<<16))/r_vert.z))>>16;
  s_vert.y = ((int64_t)(offs->y<<16)+(((int64_t)r_vert.y*(proj<<16))/r_vert.z))>>16;
  s_vert.z = r_vert.z;
  if (s_vert.x < -0x400 || s_vert.x > 0x3FF
    || s_vert.y < -0x400 || s_vert.y > 0x3FF) {
    res = 0;
  }
  *out=s_vert;
  return res;
}

#define SwZeroMat(m) \
m[0][0]=0;m[0][1]=0;m[0][2]=0; \
m[1][0]=0;m[1][1]=0;m[1][2]=0; \
m[2][0]=0;m[2][1]=0;m[2][2]=0;
#define SwDiagMat(m,v) \
m[0][0]=(v)->x;m[0][1]=     0;m[0][2]=     0; \
m[1][0]=     0;m[1][1]=(v)->y;m[1][2]=     0; \
m[2][0]=     0;m[2][1]=     0;m[2][2]=(v)->z;
#define SwCopyMat(d,s) \
d[0][0]=s[0][0];d[0][1]=s[0][1];d[0][2]=s[0][2]; \
d[1][0]=s[1][0];d[1][1]=s[1][1];d[1][2]=s[1][2]; \
d[2][0]=s[2][0];d[2][1]=s[2][1];d[2][2]=s[2][2];
#define SwZeroMatrix(d)   SwZeroMat((d)->m)
#define SwDiagMatrix(d,v) SwDiagMat((d)->m,v)
#define SwCopyMatrix(d,s) SwCopyMat((d)->m,(s)->m)

void SwTransposeMatrix(mat16 *m) {
  mat16_t t;
  SwCopyMat(t, m->m);
  m->m[0][1]=t[1][0];
  m->m[0][2]=t[2][0];
  m->m[1][0]=t[0][1];
  m->m[1][2]=t[2][1];
  m->m[2][0]=t[0][2];
  m->m[2][1]=t[1][2];
}

void SwMulMatrix(mat16 *m_l, mat16 *m_r) {
  mat16_t a,b,t;

  SwCopyMat(a, m_l->m);
  SwCopyMat(b, m_r->m);
  t[0][0]=(a[0][0]*b[0][0]+a[0][1]*b[1][0]+a[0][2]*b[2][0])>>12;
  t[0][1]=(a[0][0]*b[0][1]+a[0][1]*b[1][1]+a[0][2]*b[2][1])>>12;
  t[0][2]=(a[0][0]*b[0][2]+a[0][1]*b[1][2]+a[0][2]*b[2][2])>>12;
  t[1][0]=(a[1][0]*b[0][0]+a[1][1]*b[1][0]+a[1][2]*b[2][0])>>12;
  t[1][1]=(a[1][0]*b[0][1]+a[1][1]*b[1][1]+a[1][2]*b[2][1])>>12;
  t[1][2]=(a[1][0]*b[0][2]+a[1][1]*b[1][2]+a[1][2]*b[2][2])>>12;
  t[2][0]=(a[2][0]*b[0][0]+a[2][1]*b[1][0]+a[2][2]*b[2][0])>>12;
  t[2][1]=(a[2][0]*b[0][1]+a[2][1]*b[1][1]+a[2][2]*b[2][1])>>12;
  t[2][2]=(a[2][0]*b[0][2]+a[2][1]*b[1][2]+a[2][2]*b[2][2])>>12;
  SwCopyMat(m_l->m, t);
}

void SwRotMatrixYXY(mat16 *m_rot, ang *rot) {
  int16_t sx, sy, sz, cx, cy, cz;
  int16_t sxsy, sxsz, sysz, cxsy, cxsz, sxcy, sxcz, sycz, cxcz, cycz;
  int16_t sxcysz, cxcysz, sxcycz, cxcycz;

  sx=sin(rot->x);sy=sin(rot->y);sz=sin(rot->z);
  cx=cos(rot->x);cy=cos(rot->y);cz=cos(rot->z);
  sxsy=(sx*sy)>>12;
  sxsz=(sx*sz)>>12;
  sysz=(sy*sz)>>12;
  cxsy=(cx*sy)>>12;
  cxsz=(cx*sz)>>12;
  sxcy=(sx*cy)>>12;
  sxcz=(sx*cz)>>12;
  sycz=(sy*cz)>>12;
  cxcz=(cx*cz)>>12;
  cycz=(cy*cz)>>12;
  sxcysz=(sxcy*sz)>>12;
  sxcycz=(sxcy*cz)>>12;
  cxcysz=(cxsz*cy)>>12;
  cxcycz=(cxcz*cy)>>12;
  m_rot->m[0][0]=cxcz-sxcysz;
  m_rot->m[0][1]=sysz;
  m_rot->m[0][2]=sxcz+cxcysz;
  m_rot->m[1][0]=sxsy;
  m_rot->m[1][1]=cy;
  m_rot->m[1][2]=-cxsy;
  m_rot->m[2][0]=-cxsz-sxcycz;
  m_rot->m[2][1]=sycz;
  m_rot->m[2][2]=-sxsz+cxcycz;
}

void SwRotMatrixZXY(mat16 *m_rot, ang *rot) {
  int16_t sx, sy, sz, cx, cy, cz;
  int16_t sxsy, sxsz, cxsz, cysz, sxcy, sxcz, cxcy, cxcz, cycz;
  int16_t sxsysz, cxsysz, sxsycz, cxsycz;

  sx=sin(rot->x);sy=sin(rot->y);sz=sin(rot->z);
  cx=cos(rot->x);cy=cos(rot->y);cz=cos(rot->z);
  sxsy=(sx*sy)>>12;
  sxsz=(sx*sz)>>12;
  cxsz=(cx*sz)>>12;
  cysz=(cy*sz)>>12;
  sxcy=(sx*cy)>>12;
  sxcz=(sx*cz)>>12;
  cxcy=(cx*cy)>>12;
  cxcz=(cx*cz)>>12;
  cycz=(cy*cz)>>12;
  sxsysz=(sxsy*sz)>>12;
  cxsysz=(cxsz*sy)>>12;
  sxsycz=(sxsy*cz)>>12;
  cxsycz=(cxcz*sy)>>12;
  m_rot->m[0][0]=cxcz-sxsysz;
  m_rot->m[0][1]=-cysz;
  m_rot->m[0][2]=sxcz+cxsysz;
  m_rot->m[1][0]=cxsz+sxsycz;
  m_rot->m[1][1]=cycz;
  m_rot->m[1][2]=sxsz-cxsycz;
  m_rot->m[2][0]=-sxcy;
  m_rot->m[2][1]=sy;
  m_rot->m[2][2]=cxcy;
}

void SwScaleMatrix(mat16 *m_scale, vec *v) {
  SwDiagMatrix(m_scale, v);
}

void SwCalcObjectMatrices(
  mat16 *m_rot,
  tgeo_header *header,
  gool_vectors *vectors,
  gool_colors *colors,
  sw_transform_struct *params) {
  mat16 m_lrot, m_light, m_color;
  ang lrot;

  SwCalcObjectRotMatrix(m_rot, header, vectors, params);
  if (vectors->scale.x >= 0)
    lrot.x = vectors->rot.x - vectors->rot.z;
  else
    lrot.x = -vectors->rot.x - vectors->rot.z;
  lrot.y = vectors->rot.y;
  lrot.z = vectors->rot.z;
  lrot.x = angle12(-lrot.x);
  lrot.y = angle12(-lrot.y);
  lrot.z = angle12(-lrot.z);
  SwRotMatrixYXY(&m_lrot, &lrot);
  SwCopyMatrix(&m_light, &colors->light_mat);
  if (vectors->scale.x < 0) {
    m_light.m[0][0] = -m_light.m[0][0];
    m_light.m[1][0] = -m_light.m[1][0];
    m_light.m[2][0] = -m_light.m[2][0];
  }
  SwTransposeMatrix(&m_lrot);
  SwMulMatrix(&m_light, &m_lrot);
  SwCopyMatrix(&params->m_light, &m_light);
  SwCopyMatrix(&m_color, &colors->color_mat);
  SwTransposeMatrix(&m_color);
  SwCopyMatrix(&params->m_color, &m_color);
  params->back_color.r = (colors->color.r*colors->intensity.r)>>8;
  params->back_color.g = (colors->color.g*colors->intensity.g)>>8;
  params->back_color.b = (colors->color.b*colors->intensity.b)>>8;
}

void SwCalcObjectRotMatrix(
  mat16 *m_rot,
  tgeo_header *header,
  gool_vectors *vectors,
  sw_transform_struct *params) {
  mat16 m_rot2, m_scale;
  ang rot;
  vec scale;

  rot.x = angle12(vectors->rot.x - vectors->rot.z);
  rot.y = angle12(vectors->rot.y);
  rot.z = angle12(vectors->rot.z);
  scale.x = (int16_t)((vectors->scale.x*header->scale_x)>>12);
  scale.y = (int16_t)((vectors->scale.y*header->scale_y)>>12);
  scale.z = (int16_t)((vectors->scale.z*header->scale_z)>>12);
  SwCopyMatrix(&m_rot2, m_rot);
  SwRotMatrixYXY(&params->m_rot, &rot);
  SwMulMatrix(&m_rot2, &params->m_rot);
  SwCopyMatrix(&params->m_rot, &m_rot2);
  SwDiagMatrix(&m_scale, &scale);
  SwMulMatrix(&params->m_rot, &m_scale);
  params->m_rot.m[1][0] /= 8; params->m_rot.m[1][0] *= -5;
  params->m_rot.m[1][1] /= 8; params->m_rot.m[1][1] *= -5;
  params->m_rot.m[1][2] /= 8; params->m_rot.m[1][2] *= -5;
  params->m_rot.m[2][0] = -params->m_rot.m[2][0];
  params->m_rot.m[2][1] = -params->m_rot.m[2][1];
  params->m_rot.m[2][2] = -params->m_rot.m[2][2];
}

int SwCalcSpriteRotMatrix(
  gool_vectors *obj_vectors,
  gool_vectors *cam_vectors,
  int flag,
  int shrink,
  mat16 *m_rot,
  int32_t depth,
  int32_t max_depth,
  sw_transform_struct *params) {
  mat16 m_rot2, m_scale;
  vec trans, scale;
  ang rot;

  if (flag==0) {
    trans.x = (obj_vectors->trans.x - cam_vectors->trans.x) >> 8;
    trans.y = (obj_vectors->trans.y - cam_vectors->trans.y) >> 8;
    trans.z = (obj_vectors->trans.z - cam_vectors->trans.z) >> 8;
    SwRot(&trans, &params->trans, m_rot);
    //if (trans.z <= depth
    // || min(12000,max_depth?max_depth:12000) < trans.z)
    //  return 0;
    rot.y = ((int32_t)(cam_vectors->rot.y << 21)) >> 21;
    rot.x = ((int32_t)(cam_vectors->rot.x << 21)) >> 21;
    rot.z = ((int32_t)(cam_vectors->rot.z << 20)) >> 20;
    rot.x = obj_vectors->rot.x - limit(rot.x,-170,170);
    rot.y = obj_vectors->rot.y - limit(rot.y,-170,170);
    rot.z = obj_vectors->rot.z - rot.z;
  }
  else {
    trans.x = obj_vectors->trans.x >> 8;
    trans.y = -obj_vectors->trans.y >> 8;
    trans.z = depth;
    params->trans = trans;
    rot = obj_vectors->rot;
  }
  scale.x = obj_vectors->scale.x >> shrink;
  scale.y = obj_vectors->scale.y >> shrink;
  scale.z = obj_vectors->scale.z >> shrink;
  SwRotMatrixZXY(&params->m_rot, &rot);
  SwDiagMatrix(&m_scale, &scale);
  SwMulMatrix(&params->m_rot, &m_scale);
  params->m_rot.m[1][0] /= 8; params->m_rot.m[1][0] *= -5;
  params->m_rot.m[1][1] /= 8; params->m_rot.m[1][1] *= -5;
  params->m_rot.m[1][2] /= 8; params->m_rot.m[1][2] *= -5;
  params->m_rot.m[2][0] = 0; /* limit rotation to xy plane */
  params->m_rot.m[2][1] = 0;
  params->m_rot.m[2][2] = 0;
  return 1;
}

void SwTransformSvtx(
  svtx_frame *frame,
  void *ot,
  tgeo_polygon *polys,
  tgeo_header *header,
  int32_t cull_face,
  uint32_t far,
  void **prims_tail,
  rect28 *regions,
  int flag,
  sw_transform_struct *params) {
  svtx_vertex *verts, *vert, *poly_verts[3];
  tgeo_polygon *poly;
  tgeo_texinfo *texinfos, info;
  vec u_vert, r_verts[3], n, ldir;
  fvec uvs[4]; /* only first 3 are used */
  mat16_t lm, cm;
  rgb8 color, bc;
  srgb color16;
  poly3i *prim, *next;
  int32_t ndot;
  int32_t z_min, z_max, z_sum;
  uint32_t i;
  int ii,texid,z_idx,res;

  SwCopyMat(lm, params->m_light.m);
  SwCopyMat(cm, params->m_color.m);
  bc=params->back_color;
  verts = frame->vertices;
  texinfos = header->texinfos;
  for (i=0;i<header->poly_count;i++) {
    poly=&polys[i];
    info=*(tgeo_texinfo*)&((uint32_t*)texinfos)[poly->texinfo_idx];
    poly_verts[0]=(svtx_vertex*)((uint8_t*)verts+poly->a_idx);
    poly_verts[1]=(svtx_vertex*)((uint8_t*)verts+poly->b_idx);
    poly_verts[2]=(svtx_vertex*)((uint8_t*)verts+poly->c_idx);
    for (ii=0;ii<3;ii++) {
      vert=poly_verts[ii];
      u_vert.x=((frame->x-128)+vert->x)*4;
      u_vert.y=((frame->y-128)+vert->y)*4;
      u_vert.z=((frame->z-128)+vert->z)*4;
      res = SwRotTransPers(&u_vert, &r_verts[ii], &params->trans, &params->m_rot,
        &params->screen, params->screen_proj);
      if (!res) { break; }
    }
    if (!res) { continue; }
    if (!info.colinfo.no_cull) {
      ndot = (r_verts[0].x*r_verts[1].y) + (r_verts[1].x*r_verts[2].y)
           + (r_verts[2].x*r_verts[0].y) - (r_verts[0].x*r_verts[2].y)
           - (r_verts[1].x*r_verts[0].y) - (r_verts[2].x*r_verts[1].y);
      if (ndot == 0 || (ndot ^ cull_face) < 0) { continue; }
    }
    prim=(poly3i*)*prims_tail;
    if (poly->is_flat_shaded) {
      color.r=info.colinfo.r;
      color.g=info.colinfo.g;
      color.b=info.colinfo.b;
      prim->colors[0]=Rgb8ToA32(color);
      prim->colors[1]=Rgb8ToA32(color);
      prim->colors[2]=Rgb8ToA32(color);
    }
    else {
      for (ii=0;ii<3;ii++) {
        vert=poly_verts[ii];
        n.x=(int32_t)vert->normal_x*256;
        n.y=(int32_t)vert->normal_y*256;
        n.z=(int32_t)vert->normal_z*256;
        ldir.x=(((int64_t)lm[0][0]*n.x)+((int64_t)lm[0][1]*n.y)+((int64_t)lm[0][2]*n.z))>>12;
        ldir.y=(((int64_t)lm[1][0]*n.x)+((int64_t)lm[1][1]*n.y)+((int64_t)lm[1][2]*n.z))>>12;
        ldir.z=(((int64_t)lm[2][0]*n.x)+((int64_t)lm[2][1]*n.y)+((int64_t)lm[2][2]*n.z))>>12;
        ldir.x=limit(ldir.x,0,0x7FFF);
        ldir.y=limit(ldir.y,0,0x7FFF);
        ldir.z=limit(ldir.z,0,0x7FFF);
        color16.r=((int64_t)((int64_t)(bc.r<<12))
               +(cm[0][0]*ldir.x)+(cm[0][1]*ldir.y)+(cm[0][2]*ldir.z))>>12;
        color16.g=((int64_t)((int64_t)(bc.g<<12))
               +(cm[1][0]*ldir.x)+(cm[1][1]*ldir.y)+(cm[1][2]*ldir.z))>>12;
        color16.b=((int64_t)((int64_t)(bc.b<<12))
               +(cm[2][0]*ldir.x)+(cm[2][1]*ldir.y)+(cm[2][2]*ldir.z))>>12;
        color16.r=limit(color16.r,-0x8000,0x7FFF);
        color16.g=limit(color16.g,-0x8000,0x7FFF);
        color16.b=limit(color16.b,-0x8000,0x7FFF);
        color16.r=((int16_t)(info.colinfo.r<<4)*color16.r)>>12;
        color16.g=((int16_t)(info.colinfo.g<<4)*color16.g)>>12;
        color16.b=((int16_t)(info.colinfo.b<<4)*color16.b)>>12;
        color16.r=limit(color16.r,-0x8000,0x7FFF);
        color16.g=limit(color16.g,-0x8000,0x7FFF);
        color16.b=limit(color16.b,-0x8000,0x7FFF);
        color.r=limit(color16.r>>4,0,0xFF);
        color.g=limit(color16.g>>4,0,0xFF);
        color.b=limit(color16.b>>4,0,0xFF);
        prim->colors[ii]=Rgb8ToA32(color);
      }
    }
    if (info.colinfo.type == 1) { /* textured poly? */
      texid = TextureLoad((texinfo*)&info, &uvs);
    }
    else
      texid = -1;
    for (ii=0;ii<3;ii++) {
      prim->verts[ii]=r_verts[ii];
      prim->uvs[ii]=uvs[ii];
    }
    prim->texid = texid;
    prim->flags = info.colinfo.semi_trans;
    if (flag) {
      z_min=min(min(r_verts[0].z,r_verts[1].z),r_verts[2].z);
      z_max=max(max(r_verts[0].z,r_verts[1].z),r_verts[2].z);
      z_sum=(3*(z_min+z_max))/2;
    }
    else
      z_sum = r_verts[0].z+r_verts[1].z+r_verts[2].z;
    z_idx = far - (z_sum/32);
    z_idx = limit(z_idx, 0, 0x7FF);
    next = ((poly3i**)ot)[z_idx];
    prim->prim.next = next;
    prim->prim.type = 1;
    ((poly3i**)ot)[z_idx] = prim;
    *prims_tail+=sizeof(poly3i);
  }
}

void SwTransformCvtx(
  cvtx_frame *frame,
  void *ot,
  tgeo_polygon *polys,
  tgeo_header *header,
  int32_t cull_face,
  uint32_t far,
  void **prims_tail,
  rect28 *regions,
  int shamt,
  sw_transform_struct *params) {
  cvtx_vertex *verts, *vert, *poly_verts[3];
  tgeo_polygon *poly;
  tgeo_texinfo *texinfos, info;
  vec u_vert, r_verts[3], n, ldir;
  fvec uvs[4]; /* only first 3 are used */
  mat16_t lm, cm;
  rgb8 color, bc;
  rgb16 color16;
  poly3i *prim, *next;
  int32_t ndot;
  int32_t z_min, z_max, z_sum;
  int32_t t1, t2, t3;
  uint32_t i;
  int ii, texid, z_idx, res;
  int idx_adj;

  SwCopyMat(lm, params->m_light.m);
  SwCopyMat(cm, params->m_color.m);
  bc=params->back_color;
  verts = frame->vertices;
  texinfos = header->texinfos;
  for (i=0;i<header->poly_count;i++) {
    poly=&polys[i];
    info=*(tgeo_texinfo*)&((uint32_t*)texinfos)[poly->texinfo_idx];
    poly_verts[0]=(cvtx_vertex*)((uint8_t*)verts+poly->a_idx);
    poly_verts[1]=(cvtx_vertex*)((uint8_t*)verts+poly->b_idx);
    poly_verts[2]=(cvtx_vertex*)((uint8_t*)verts+poly->c_idx);
    for (ii=0;ii<3;ii++) {
      vert=poly_verts[ii];
      u_vert.x=((frame->x-128)+vert->x)*4;
      u_vert.y=((frame->y-128)+vert->y)*4;
      u_vert.z=((frame->z-128)+vert->z)*4;
      res = SwRotTransPers(&u_vert, &r_verts[ii], &params->trans, &params->m_rot,
        &params->screen, params->screen_proj);
      if (!res) { break; }
    }
    if (!res) { continue; }
    idx_adj = 0;
    ndot = (r_verts[0].x*r_verts[1].y) + (r_verts[1].x*r_verts[2].y)
         + (r_verts[2].x*r_verts[0].y) - (r_verts[0].x*r_verts[2].y)
         - (r_verts[1].x*r_verts[0].y) - (r_verts[2].x*r_verts[1].y);
    if (ndot == 0 || (ndot ^ cull_face) < 0) {
      if (!info.colinfo.no_cull) { continue; }
      idx_adj = 12;
    }
    prim=(poly3i*)*prims_tail;
    t1=info.colinfo.t1;t2=info.colinfo.t2;t3=info.colinfo.t3;
    for (ii=0;ii<3;ii++) {
      vert=poly_verts[ii];
      color.r = ((t1>=0 ?
          (127-abs(t1))*0   +     (abs(t1)*(int32_t)vert->r)
        : (128-abs(t1))*255 + ((abs(t1)-1)*(int32_t)vert->r))*32)>>(shamt+12);
      color.g = ((t2>=0 ?
          (127-abs(t2))*0   +     (abs(t2)*(int32_t)vert->g)
        : (128-abs(t2))*255 + ((abs(t2)-1)*(int32_t)vert->g))*32)>>(shamt+12);
      color.b = ((t3>=0 ?
          (127-abs(t3))*0   +     (abs(t3)*(int32_t)vert->b)
        : (128-abs(t3))*255 + ((abs(t3)-1)*(int32_t)vert->b))*32)>>(shamt+12);
      prim->colors[ii]=Rgb8ToA32(color);
    }
    if (info.colinfo.type == 1) { /* textured poly? */
      texid = TextureLoad((texinfo*)&info, &uvs);
    }
    else
      texid = -1;
    for (ii=0;ii<3;ii++) {
      prim->verts[ii]=r_verts[ii];
      prim->uvs[ii]=uvs[ii];
    }
    prim->texid = texid;
    prim->flags = info.colinfo.semi_trans;
    z_sum = r_verts[0].z+r_verts[1].z+r_verts[2].z;
    z_idx = far - ((z_sum/32)+idx_adj);
    z_idx = limit(z_idx, 0, 0x7FF);
    next = ((poly3i**)ot)[z_idx];
    prim->prim.next = next;
    prim->prim.type = 1;
    ((poly3i**)ot)[z_idx] = prim;
    *prims_tail+=sizeof(poly3i);
  }
}

void SwTransformSprite(
  void *ot,
  texinfo2 *info2,
  eid_t tpag,
  int size,
  uint32_t far,
  void **prims_tail,
  rect28 *regions,
  sw_transform_struct *params) {
  poly4i *prim, *next;
  texinfo info;
  vec verts[4], r_verts[4]; /* corners */
  fvec uvs[4];
  int32_t z_sum;
  int i, texid, z_idx, res;

  verts[0].x=-size;verts[0].y= size;verts[0].z=0;
  verts[1].x= size;verts[1].y= size;verts[1].z=0;
  verts[2].x=-size;verts[2].y=-size;verts[2].z=0;
  verts[3].x= size;verts[3].y=-size;verts[3].z=0;
  for (i=0;i<4;i++) {
    res = SwRotTransPers(&verts[i], &r_verts[i], &params->trans, &params->m_rot,
      &params->screen, params->screen_proj);
  }
  if (!res) { return; } /* return on fail */
  prim=(poly4i*)*prims_tail;
  info.colinfo = info2->colinfo;
  info.rgninfo = info2->rgninfo;
  info.tpage = tpag;
  texid=TextureLoad(&info, &uvs);
  for (i=0;i<4;i++) {
    prim->verts[i]=r_verts[i];
    prim->colors[i]=Rgb8ToA32(info.colinfo.rgb);
    prim->uvs[i]=uvs[i];
  }
  prim->texid=texid;
  prim->flags=info.colinfo.semi_trans;
  z_sum=prim->verts[0].z+prim->verts[1].z+prim->verts[2].z;
  z_idx=far-(z_sum/32);
  z_idx=limit(z_idx, 0, 0x7FF);
  next=((poly4i**)ot)[z_idx];
  prim->prim.next=next;
  prim->prim.type=2;
  ((poly4i**)ot)[z_idx] = prim;
  *prims_tail+=sizeof(poly4i);
}

extern mat16 ms_rot;
void SwProjectBound(bound *lbound, vec *col, vec *trans, vec *_cam_trans, bound2 *extents) {
  vec vert;
  vec2 *screen;
  vec s_vert;
  mat16 *m_rot;
  bound bound;
  int i, j, k;
  extents->p1.x = 0x7FFFFFFF; /* INT_MAX */
  extents->p1.y = 0x7FFFFFFF; /* INT_MAX */
  extents->p2.x = -0x80000000; /* INT_MIN */
  extents->p2.y = -0x80000000; /* INT_MIN */
  bound.p1.x=lbound->p1.x+trans->x+(col->x-_cam_trans->x)>>8;
  bound.p1.y=lbound->p1.y+trans->y+(col->y-_cam_trans->y)>>8;
  bound.p1.z=lbound->p1.z+trans->z+(col->z-_cam_trans->z)>>8;
  bound.p2.x=lbound->p2.x+trans->x+(col->x-_cam_trans->x)>>8;
  bound.p2.y=lbound->p2.y+trans->y+(col->y-_cam_trans->y)>>8;
  bound.p2.z=lbound->p2.z+trans->z+(col->z-_cam_trans->z)>>8;
  trans=&params.trans;
  //m_rot=&params.m_rot;
  screen=&params.screen;
  for (i=0;i<2;i++) {
    for (j=0;j<2;j++) {
      for (k=0;k<2;k++) {
        vert.x=i?bound.p1.x:bound.p2.x;
        vert.y=j?bound.p1.y:bound.p2.y;
        vert.z=k?bound.p1.z:bound.p2.z;
        SwRotTransPers(&vert, &s_vert, trans, &ms_rot, screen, params.screen_proj);
        extents->p1.x=min(extents->p1.x, s_vert.x);
        extents->p1.y=min(extents->p1.y, s_vert.y);
        extents->p2.x=max(extents->p2.x, s_vert.x);
        extents->p2.y=max(extents->p2.y, s_vert.y);
      }
    }
  }
}

/* shaders use this to identify the input vertex */
typedef struct {
  uint32_t vert_idx:2;
  uint32_t poly_idx:12;
  uint32_t world_idx:3;
  uint32_t flag:1;
} vert_id;

typedef void (*sw_shader_t)(vert_id, vec*, rgb8*, sw_transform_struct*);

static inline wgeo_vertex *SwWorldVertex(vert_id vert_id) {
  zone_header *header;
  zone_world *world;
  wgeo_polygon *poly;
  wgeo_vertex *vert;
  int world_idx, poly_idx, vert_idx;

  world_idx=vert_id.world_idx;
  poly_idx=vert_id.poly_idx;
  vert_idx=vert_id.vert_idx;
  header=(zone_header*)cur_zone->items[0];
  world=&header->worlds[world_idx];
  poly=&world->polygons[poly_idx];
  switch (vert_idx) {
  case 0: vert_idx=poly->a_idx; break;
  case 1: vert_idx=poly->b_idx; break;
  case 2: vert_idx=poly->c_idx; break;
  }
  vert=&world->vertices[vert_idx];
  return vert;
}

static void SwTransformAndShadeWorlds(
  poly_id_list *poly_id_list,
  void *ot,
  int32_t far,
  int anim_counter,
  void **prims_tail,
  sw_transform_struct *params,
  sw_shader_t pre_shader,
  sw_shader_t post_shader) {
  zone_header *header;
  zone_world *worlds, *world;
  poly_id *poly_ids;
  poly_id poly_id;
  vert_id vert_id;
  wgeo_polygon *polys, *poly;
  wgeo_vertex *verts, *vert, *poly_verts[3];
  wgeo_texinfo *texinfos, *wgeo_info;
  texinfo info;
  eid_t *tpags, tpag;
  vec trans, u_verts[3], r_verts[3], v_verts[3];
  rgb8 colors[3];
  fvec uvs[4]; /* only first 3 are used */
  poly3i *prim, *next;
  int32_t z_sum;
  int texid, z_idx, min_z_idx, offs;
  int i, ii, world_idx, poly_idx, info_idx;

  worlds = params->worlds;
  poly_ids = poly_id_list->ids;
  min_z_idx = 0x7FF;
  for (i=poly_id_list->len-1;i>=0;i--) {
    poly_id = poly_ids[i];
    world_idx = poly_id.world_idx;
    world = &worlds[world_idx];
    polys = world->polygons;
    verts = world->vertices;
    texinfos = world->texinfos;
    tpags = world->header->tpags;
    trans = world->trans;
    poly_idx = poly_id.poly_idx;
    poly = &world->polygons[poly_idx];
    poly_verts[0] = &verts[poly->a_idx];
    poly_verts[1] = &verts[poly->b_idx];
    poly_verts[2] = &verts[poly->c_idx];
    for (ii=0;ii<3;ii++) {
      vert = poly_verts[ii];
      u_verts[ii].x = (int16_t)(vert->x*8);
      u_verts[ii].y = (int16_t)(vert->y*8);
      u_verts[ii].z = (int16_t)((vert->z8+(vert->z2<<8)+(vert->z3<<10))*8);
      colors[ii].r = vert->r;
      colors[ii].g = vert->g;
      colors[ii].b = vert->b;
      if (pre_shader) {
        vert_id.world_idx = world_idx;
        vert_id.poly_idx = poly_idx;
        vert_id.vert_idx = ii;
        pre_shader(vert_id, &u_verts[ii], &colors[ii], params);
      }
      SwRotTransPers(&u_verts[ii], &r_verts[ii], &world->trans, &params->m_rot,
        &params->screen, params->screen_proj);
      if (post_shader) {
        vert_id.world_idx = world_idx;
        vert_id.poly_idx = poly_idx;
        vert_id.vert_idx = ii;
        post_shader(vert_id, &r_verts[ii], &colors[ii], params);
      }
    }
    info_idx = poly->tinf_idx;
    tpag = tpags[poly->tpag_idx];
    wgeo_info = (wgeo_texinfo*)(&((uint32_t*)texinfos)[info_idx]);
    info.colinfo = wgeo_info->colinfo; /* convert to generic texinfo */
    if (info.colinfo.type == 1) {
       if (poly->anim_mask) {
         info_idx = anim_counter >> poly->anim_period;
         info_idx = (poly->anim_phase+info_idx) & ((poly->anim_mask<<1)|1);
         info.rgninfo = wgeo_info->rgninfos[info_idx];
       }
       else
         info.rgninfo = wgeo_info->rgninfo;
       info.tpage = tpag;
       texid = TextureLoad(&info, &uvs);
    }
    else { texid = -1; }
    prim = (poly3i*)*prims_tail;
    for (ii=0;ii<3;ii++) {
      prim->verts[ii] = r_verts[ii];
      prim->colors[ii] = Rgb8ToA32(colors[ii]);
      prim->uvs[ii] = uvs[ii];
    }
    prim->texid = texid;
    prim->flags = info.colinfo.semi_trans;
    z_sum = prim->verts[0].z+prim->verts[1].z+prim->verts[2].z;
    z_idx = far - (z_sum/32);
    z_idx = limit(z_idx, 0, 0x7FF);
    if (z_idx < min_z_idx)
      min_z_idx = z_idx;
    else
      z_idx = min_z_idx;
    next = ((poly3i**)ot)[z_idx];
    prim->prim.next = next;
    prim->prim.type = 1;
    ((poly3i**)ot)[z_idx] = prim;
    *prims_tail += sizeof(poly3i);
  }
}

static void SwFogShader(vert_id vert_id, vec *vert, rgb8 *color, sw_transform_struct *params) {
  zone_header *header;
  zone_world *world;
  srgb c1, c2, cd;
  int32_t t, far;
  int world_idx, shamt;

  world_idx=vert_id.world_idx;
  world=&params->worlds[world_idx];
  far=world->tag & 0xFFFF;
  shamt=(world->tag >> 16) & 0xFFFF;
  if (vert->z > far) { /* transformed z is greater than far value in world tag? */
    /* compute t value for interpolation as distance from far, shifted left by shamt */
    t = (vert->z - far) << shamt; /* 12 bit fractional fixed point */
    /* convert integer-valued color components
       to normalized 12 bit fractional fixed point */
    c1.r = ((int32_t)color->r)<<4;
    c1.g = ((int32_t)color->g)<<4;
    c1.b = ((int32_t)color->b)<<4;
    c2.r = ((int32_t)params->far_color1.r)<<4;
    c2.g = ((int32_t)params->far_color1.g)<<4;
    c2.b = ((int32_t)params->far_color1.b)<<4;
    cd.r = limit((c2.r-c1.r), -0x800, 0x7FF);
    cd.g = limit((c2.g-c1.g), -0x800, 0x7FF);
    cd.b = limit((c2.b-c1.b), -0x800, 0x7FF);
    /* interpolate between vert color and far color using t value
       (multiplication of 2 12 bit fractional fixed point values
       produces a 24 bit fixed point value, so shift right by 16
       to convert back to the 0 to 255 range) */
    color->r = limit(((c1.r<<12)+(t*cd.r))>>16,0,255);
    color->g = limit(((c1.g<<12)+(t*cd.g))>>16,0,255);
    color->b = limit(((c1.b<<12)+(t*cd.b))>>16,0,255);
  }
}

static void SwRippleShader(vert_id vert_id, vec *vert, rgb8 *color, sw_transform_struct *params) {
  wgeo_vertex *world_vert;
  int idx;

  world_vert = SwWorldVertex(vert_id);
  if (world_vert->fx) {
    idx = ((vert->x+vert->y)/8) & 0xF;
    vert->y += params->tri_wave[idx];
  }
}

static void SwLightningShader(vert_id vert_id, vec *vert, rgb8 *color, sw_transform_struct *params) {
  wgeo_vertex *world_vert;
  rgb8 *far_color;
  srgb c1, c2, cd;
  int32_t t;

  world_vert = SwWorldVertex(vert_id);
  if (!world_vert->fx) {
    t = params->far_t1/16;
    far_color = &params->far_color1;
  }
  else {
    t = params->far_t2/16;
    far_color = &params->far_color2;
  }
  /* convert integer-valued color components
  to normalized 12 bit fractional fixed point */
  c1.r = ((int32_t)color->r)<<4;
  c1.g = ((int32_t)color->g)<<4;
  c1.b = ((int32_t)color->b)<<4;
  c2.r = ((int32_t)far_color->r)<<4;
  c2.g = ((int32_t)far_color->g)<<4;
  c2.b = ((int32_t)far_color->b)<<4;
  cd.r = limit((c2.r-c1.r), -0x800, 0x7FF);
  cd.g = limit((c2.g-c1.g), -0x800, 0x7FF);
  cd.b = limit((c2.b-c1.b), -0x800, 0x7FF);
  /* interpolate between vert color and far color using t value
     (multiplication of 2 12 bit fractional fixed point values
     produces a 24 bit fixed point value, so shift right by 16
     to convert back to the 0 to 255 range) */
  color->r = limit(((c1.r<<12)+(t*cd.r))>>16,0,255);
  color->g = limit(((c1.g<<12)+(t*cd.g))>>16,0,255);
  color->b = limit(((c1.b<<12)+(t*cd.b))>>16,0,255);
}

static void SwDarkShader(vert_id vert_id, vec *vert, rgb8 *color, sw_transform_struct *params) {
  SwLightningShader(vert_id, vert, color, params);
  SwFogShader(vert_id, vert, color, params);
}

static void SwDark2Shader(vert_id vert_id, vec *vert, rgb8 *color, sw_transform_struct *params) {
  zone_header *header;
  zone_world *world;
  wgeo_vertex *world_vert;
  vec delta;
  srgb c1, c2, cd;
  int32_t t;
  int world_idx;

  world_idx=vert_id.world_idx;
  header=(zone_header*)cur_zone->items[0];
  world=&header->worlds[world_idx];
  world_vert = SwWorldVertex(vert_id);
  delta.x = vert->x + world->trans.x - params->illum.x;
  delta.y = vert->y + world->trans.y - params->illum.y;
  delta.z = vert->z + world->trans.z - params->illum.z;
  t = abs(delta.x)+abs(delta.y)+abs(delta.z);
  t += (t >> params->shamt_add) - (t >> params->shamt_sub);
  t += !world_vert->fx ? params->amb_fx0 : params->amb_fx1;
  t = limit(t, 0, 4096);
  /* convert integer-valued color components
  to normalized 12 bit fractional fixed point */
  c1.r = ((int32_t)color->r)<<4;
  c1.g = ((int32_t)color->g)<<4;
  c1.b = ((int32_t)color->b)<<4;
  c2.r = ((int32_t)params->far_color1.r)<<4;
  c2.g = ((int32_t)params->far_color1.g)<<4;
  c2.b = ((int32_t)params->far_color1.b)<<4;
  cd.r = limit((c2.r-c1.r), -0x800, 0x7FF);
  cd.g = limit((c2.g-c1.g), -0x800, 0x7FF);
  cd.b = limit((c2.b-c1.b), -0x800, 0x7FF);
  /* interpolate between vert color and far color using t value
     (multiplication of 2 12 bit fractional fixed point values
     produces a 24 bit fixed point value, so shift right by 16
     to convert back to the 0 to 255 range) */
  color->r = limit(((c1.r<<12)+(t*cd.r))>>16,0,255);
  color->g = limit(((c1.g<<12)+(t*cd.g))>>16,0,255);
  color->b = limit(((c1.b<<12)+(t*cd.b))>>16,0,255);
}

void SwTransformWorlds(
  poly_id_list *poly_id_list,
  void *ot,
  int32_t proj,
  int anim_phase,
  void **prims_tail,
  sw_transform_struct *params) {
  SwTransformAndShadeWorlds(poly_id_list,ot,proj,anim_phase,prims_tail,params,0,0);
}

void SwTransformWorldsFog(
  poly_id_list *poly_id_list,
  void *ot,
  int32_t proj,
  int anim_phase,
  void **prims_tail,
  sw_transform_struct *params) {
  SwTransformAndShadeWorlds(poly_id_list,ot,proj,anim_phase,prims_tail,params,0,SwFogShader);
}

void SwTransformWorldsRipple(
  poly_id_list *poly_id_list,
  void *ot,
  int32_t proj,
  int anim_phase,
  void **prims_tail,
  sw_transform_struct *params) {
  SwTransformAndShadeWorlds(poly_id_list,ot,proj,anim_phase,prims_tail,params,SwRippleShader,0);
}

void SwTransformWorldsLightning(
  poly_id_list *poly_id_list,
  void *ot,
  int32_t proj,
  int anim_phase,
  void **prims_tail,
  sw_transform_struct *params) {
  SwTransformAndShadeWorlds(poly_id_list,ot,proj,anim_phase,prims_tail,params,0,SwLightningShader);
}

void SwTransformWorldsDark(
  poly_id_list *poly_id_list,
  void *ot,
  int32_t proj,
  int anim_phase,
  void **prims_tail,
  sw_transform_struct *params) {
  SwTransformAndShadeWorlds(poly_id_list,ot,proj,anim_phase,prims_tail,params,0,SwDarkShader);
}

void SwTransformWorldsDark2(
  poly_id_list *poly_id_list,
  void *ot,
  int32_t proj,
  int anim_phase,
  void **prims_tail,
  sw_transform_struct *params) {
  SwTransformAndShadeWorlds(poly_id_list,ot,proj,anim_phase,prims_tail,params,0,SwDark2Shader);
}

#ifdef CFLAGS_DRAW_EXTENSIONS

#include "level.h"
extern gool_vectors cam;
extern mat16 ms_cam_rot;
const int rface_vert_idxes[6][4] = { { 0, 1, 2, 3 },
                                     { 1, 5, 3, 7 },
                                     { 4, 5, 6, 7 },
                                     { 0, 4, 2, 6 },
                                     { 4, 5, 0, 1 },
                                     { 6, 7, 2, 3 } };
/* extensions */
void SwTransformZoneQuery(zone_query *query, void *ot, void **prims_tail) {
  zone_query_result *result;
  zone_query_result_rect *result_rect;
  bound nbound, v_nbound, *nodes_bound;
  dim zone_dim;
  vec max_depth, u_vert, r_verts[8], zero;
  rgb8 color;
  poly4i *prim, *next;
  int16_t node;
  int i, ii, iii, level, type, subtype;

  zero.x = 0; zero.y = 0; zero.z = 0;
  nodes_bound = &query->nodes_bound;
  result = &query->results[0];
  for (i=0;i<query->result_count;i++) {
    if (result->value == -1) { break; }
    if (!result->is_node) {
      result_rect = (zone_query_result_rect*)result;
      zone_dim.w = result_rect->w << 8;
      zone_dim.h = result_rect->h << 8;
      zone_dim.d = result_rect->d << 8;
      max_depth.x = result_rect->max_depth_x;
      max_depth.y = result_rect->max_depth_y;
      max_depth.z = result_rect->max_depth_z;
      result += 2;
      continue;
    }
    node = (result->node << 1) | 1;
    level = result->level;
    type = (node & 0xE) >> 1;
    subtype = (node & 0x3F0) >> 4;
    nbound.p1.x = ((int32_t)result->x << 4) + nodes_bound->p1.x;
    nbound.p1.y = ((int32_t)result->y << 4) + nodes_bound->p1.y;
    nbound.p1.z = ((int32_t)result->z << 4) + nodes_bound->p1.z;
    ++result;
    nbound.p2.x = nbound.p1.x + (int32_t)(zone_dim.w >> min(level, max_depth.x));
    nbound.p2.y = nbound.p1.y + (int32_t)(zone_dim.h >> min(level, max_depth.y));
    nbound.p2.z = nbound.p1.z + (int32_t)(zone_dim.d >> min(level, max_depth.z));
    v_nbound.p1.x = nbound.p1.x >> 8;
    v_nbound.p1.y = nbound.p1.y >> 8;
    v_nbound.p1.z = nbound.p1.z >> 8;
    v_nbound.p2.x = nbound.p2.x >> 8;
    v_nbound.p2.y = nbound.p2.y >> 8;
    v_nbound.p2.z = nbound.p2.z >> 8;
    for (ii=0;ii<8;ii++) {
      if (ii%2) { u_vert.x = v_nbound.p1.x; }
      else { u_vert.x = v_nbound.p2.x; }
      if ((ii/2)%2) { u_vert.y = v_nbound.p1.y; }
      else { u_vert.y = v_nbound.p2.y; }
      if ((ii/4)%2) { u_vert.z = v_nbound.p1.z; }
      else { u_vert.z = v_nbound.p2.z; }
      u_vert.x -= (cam_trans.x >> 8);
      u_vert.y -= (cam_trans.y >> 8);
      u_vert.z -= (cam_trans.z >> 8);
      SwRotTransPers(&u_vert, &r_verts[ii], &zero, &ms_cam_rot, &params.screen, params.screen_proj);
    }
    color.r = 127+((type&1)*(subtype+1)*2);
    color.g = 127+((type&2)*(subtype+1)*2);
    color.b = 127+((type&4)*(subtype+1)*2);
    for (ii=0;ii<6;ii++) {
      prim=(poly4i*)(*prims_tail);
      for (iii=0;iii<4;iii++) {
        prim->verts[iii]=r_verts[rface_vert_idxes[ii][iii]];
        prim->colors[iii]=Rgb8ToA32(color);
      }
      prim->texid=-1;
      prim->flags=3;
      next=((poly4i**)ot)[0x7FE];
      prim->next=next;
      prim->type=3;
      ((poly4i**)ot)[0x7FE]=prim;
      *prims_tail+=sizeof(poly4i);
    }
  }
}

#include "gl.h"
int wallmap_image = 0;

void SwDrawWallMap(uint32_t *wall_bitmap, void *ot, void **prims_tail) {
  poly4i *prim, *next;
  rect2 rect;
  uint32_t pixels[32*32], *p;
  uint32_t bits, tmp;
  int i,j,idx;

  idx = 32;
  for (i=0;i<32;i++) {
    bits = wall_bitmap[i];
    for (j=0;j<32;j++) {
      idx = (i*32)+(31-j);
      p = &pixels[idx];
      pixels[idx] = bits & 1 ? -1: 0;
      bits >>= 1;
    }
    idx += 64;
  }
  rect.w=32;rect.h=32;rect.x=0;rect.y=0;
  if (wallmap_image == 0)
    wallmap_image = GLCreateTexture(rect.dim, (uint8_t*)pixels);
  else
    GLUpdateTexture(wallmap_image, rect, (uint8_t*)pixels);
  prim=(poly4i*)(*prims_tail);
  for (i=0;i<4;i++) {
    prim->verts[i].x = (i%2)?0:32;
    prim->verts[i].y = (i/2)?32:0;
    prim->verts[i].z = -1;
    prim->uvs[i].x = (i%2)?0:1.0;
    prim->uvs[i].y = (i/2)?1.0:0;
    tmp=-1;
    prim->colors[i] = Rgb8ToA32(*(rgb8*)&tmp);
  }
  prim->texid=wallmap_image;
  prim->flags=3;
  next=((poly4i**)ot)[0x7FE];
  prim->next=next;
  prim->type=2;
  ((poly4i**)ot)[0x7FE]=prim;
  *prims_tail+=sizeof(poly4i);
}

void SwTransformObjectBounds(gool_bound *bounds, int count, void *ot, void **prims_tail) {
  gool_bound *bound;
  vec max_depth, u_vert, r_verts[8], zero;
  rgb8 color;
  poly4i *prim, *next;
  int i, ii, iii;

  zero.x = 0; zero.y = 0; zero.z = 0;
  for (i=0;i<count;i++) {
    bound = &bounds[i];
    for (ii=0;ii<8;ii++) {
      if (ii%2) { u_vert.x = bound->bound.p1.x >> 8; }
      else { u_vert.x = bound->bound.p2.x >> 8; }
      if ((ii/2)%2) { u_vert.y = bound->bound.p1.y >> 8; }
      else { u_vert.y = bound->bound.p2.y >> 8; }
      if ((ii/4)%2) { u_vert.z = bound->bound.p1.z >> 8; }
      else { u_vert.z = bound->bound.p2.z >> 8; }
      u_vert.x -= (cam_trans.x >> 8);
      u_vert.y -= (cam_trans.y >> 8);
      u_vert.z -= (cam_trans.z >> 8);
      SwRotTransPers(&u_vert, &r_verts[ii], &zero, &ms_cam_rot, &params.screen, params.screen_proj);
    }
    color.r = 255;
    color.g = 255;
    color.b = 255;
    for (ii=0;ii<6;ii++) {
      prim=(poly4i*)(*prims_tail);
      for (iii=0;iii<4;iii++) {
        prim->verts[iii]=r_verts[rface_vert_idxes[ii][iii]];
        prim->colors[iii]=Rgb8ToA32(color);
      }
      prim->texid=-1;
      prim->flags=3;
      next=((poly4i**)ot)[0x7FE];
      prim->next=next;
      prim->type=3;
      ((poly4i**)ot)[0x7FE]=prim;
      *prims_tail+=sizeof(poly4i);
    }
  }
}

#endif
