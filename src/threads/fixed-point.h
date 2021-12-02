#ifndef FIXED_POINT_H
#define FIXED_POINT_H
/* Utils for fixed-point real arithmetic computations using integer 
 operations. */

/* Abstract data type for fixed-point. */
typedef int fixed_point;

/* 14 bits as fractional bits. */
#define F (1 << 14)

/* Convert from fixed-point to integer. (rounding toward zero) */
#define INT(x) ((x) / F)

/* Convert from integer to fixed-point. */
#define FLOAT(x) ((x) * F)

/* Convert from fixed-point to integer. (rounding to nearest) */
#define ROUND(x) ((x) >= 0 ? (((x) + F / 2) / F) : (((x) - F / 2) / F))

/* Add fixed-point x and int y. */
#define FADDI(x, y) ((x) + (y) * F)

/* Subtract int y from fixed-point x. */
#define FSUBI(x, y) ((x) - (y) * F)

/* Multiply fixed-point x by fixed-point y. */
#define FMULF(x, y) (((int64_t) (x)) * (y) / F)

/* Multiply fixed-point x by int y. */
#define FMULI(x, y) ((x) * (y))

/* Divide fixed-point x by fixed-point y. */
#define FDIVF(x, y) (((int64_t) (x)) * F / (y))

/* Divide fixed-point x by int y. */
#define FDIVI(x, y) ((x) / (y)) 
 
#endif 