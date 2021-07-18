#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#define F (1 << 14)
#define INT(x) ((x) / F)
#define FLOAT(x) ((x) * F)
#define ROUND(x) ((x) >= 0 ? (((x) + F / 2) / F) : (((x) - F / 2) / F))
#define FADDI(x, y) ((x) + (y) * F)
#define FSUBI(x, y) ((x) - (y) * F)
#define FMULF(x, y) (((int64_t) (x)) * (y) / F)
#define FMULI(x, y) ((x) * (y))
#define FDIVF(x, y) (((int64_t) (x)) * F / (y))
#define FDIVI(x, y) ((x) / (y)) 
 
#endif 