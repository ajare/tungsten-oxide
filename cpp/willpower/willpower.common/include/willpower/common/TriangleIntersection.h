#pragma once

#include "willpower/common/Vector2.h"

#define ORIENT_2D(a, b, c) ((a.x-c.x)*(b.y-c.y)-(a.y-c.y)*(b.x-c.x))

#define INTERSECTION_TEST_VERTEX(P1, Q1, R1, P2, Q2, R2) {\
  if (ORIENT_2D(R2,P2,Q1) >= 0.0f)\
    if (ORIENT_2D(R2,Q2,Q1) <= 0.0f)\
      if (ORIENT_2D(P1,P2,Q1) > 0.0f) {\
	if (ORIENT_2D(P1,Q2,Q1) <= 0.0f) return true; \
		else return false;} else {\
	if (ORIENT_2D(P1,P2,R1) >= 0.0f)\
	  if (ORIENT_2D(Q1,R1,P2) >= 0.0f) return true; \
	  	  else return false;\
		else return false;}\
	    else \
      if (ORIENT_2D(P1,Q2,Q1) <= 0.0f)\
	if (ORIENT_2D(R2,Q2,R1) <= 0.0f)\
	  if (ORIENT_2D(Q1,R1,Q2) >= 0.0f) return true; \
	  	  else return false;\
		else return false;\
	        else return false;\
    else\
    if (ORIENT_2D(R2,P2,R1) >= 0.0f) \
      if (ORIENT_2D(Q1,R1,R2) >= 0.0f)\
	if (ORIENT_2D(P1,P2,R1) >= 0.0f) return true;\
		else return false;\
	        else \
	if (ORIENT_2D(Q1,R1,Q2) >= 0.0f) {\
	  if (ORIENT_2D(R2,R1,Q2) >= 0.0f) return true; \
	  	  else return false; }\
		else return false; \
	    else  return false; \
  };

#define INTERSECTION_TEST_EDGE(P1, Q1, R1, P2, Q2, R2) { \
  if (ORIENT_2D(R2,P2,Q1) >= 0.0f) {\
    if (ORIENT_2D(P1,P2,Q1) >= 0.0f) { \
        if (ORIENT_2D(P1,Q1,R2) >= 0.0f) return true; \
		        else return false;} else { \
      if (ORIENT_2D(Q1,R1,P2) >= 0.0f){ \
	if (ORIENT_2D(R1,P1,P2) >= 0.0f) return true; else return false;} \
	        else return false; } \
    } else {\
    if (ORIENT_2D(R2,P2,R1) >= 0.0f) {\
      if (ORIENT_2D(P1,P2,R1) >= 0.0f) {\
	if (ORIENT_2D(P1,R1,R2) >= 0.0f) return true;  \
		else {\
	  if (ORIENT_2D(Q1,R1,R2) >= 0.0f) return true; else return false;}}\
	        else  return false; }\
	    else return false; }}

bool ccw_tri_tri_intersection_2d(WP_NAMESPACE::Vector2 const& p1, WP_NAMESPACE::Vector2 const& q1, WP_NAMESPACE::Vector2 const& r1,
	WP_NAMESPACE::Vector2 const& p2, WP_NAMESPACE::Vector2 const& q2, WP_NAMESPACE::Vector2 const& r2) {
	if (ORIENT_2D(p2, q2, p1) >= 0.0f) {
		if (ORIENT_2D(q2, r2, p1) >= 0.0f) {
			if (ORIENT_2D(r2, p2, p1) >= 0.0f) return true;
			else INTERSECTION_TEST_EDGE(p1, q1, r1, p2, q2, r2)
		}
		else {
			if (ORIENT_2D(r2, p2, p1) >= 0.0f)
				INTERSECTION_TEST_EDGE(p1, q1, r1, r2, p2, q2)
			else INTERSECTION_TEST_VERTEX(p1, q1, r1, p2, q2, r2)
		}
	}
	else {
		if (ORIENT_2D(q2, r2, p1) >= 0.0f) {
			if (ORIENT_2D(r2, p2, p1) >= 0.0f)
				INTERSECTION_TEST_EDGE(p1, q1, r1, q2, r2, p2)
			else  INTERSECTION_TEST_VERTEX(p1, q1, r1, q2, r2, p2)
		}
		else INTERSECTION_TEST_VERTEX(p1, q1, r1, r2, p2, q2)
	}
};

