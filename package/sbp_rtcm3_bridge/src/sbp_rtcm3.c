/*
 * Copyright (C) 2017 Swift Navigation Inc.
 * Contact: Jacob McNamee <jacob@swiftnav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <math.h>
#include "sbp_rtcm3.h"
#include <rtcm3_io/src/rtcm3_decode.h>
#include <assert.h>

/** Convert navigation_measurement_t.lock_time into SBP lock time.
 *
 * Note: It is encoded according to DF402 from the RTCM 10403.2 Amendment 2
 * specification.  Valid values range from 0 to 15 and the most significant
 * nibble is reserved for future use.
 *
 * \param nm_lock_time Navigation measurement lock time [s]
 * \return SBP lock time
 */
u8 encode_lock_time(double nm_lock_time) {
    assert(nm_lock_time >= 0.0);

    /* Convert to milliseconds */
    u32 ms_lock_time;
    if (nm_lock_time < UINT32_MAX) {
        ms_lock_time = (u32)(nm_lock_time * SECS_MS);
    } else {
        ms_lock_time = UINT32_MAX;
    }

    if (ms_lock_time < 32) {
        return 0;
    } else {
        for(u8 i = 0; i < 16; i++) {
            if (ms_lock_time > (1u << (i + 5))) {
                continue;
            } else {
                return i;
            }
        }
        return 15;
    }
}

/** Convert SBP lock time into navigation_measurement_t.lock_time.
 *
 * Note: It is encoded according to DF402 from the RTCM 10403.2 Amendment 2
 * specification.  Valid values range from 0 to 15 and the most significant
 * nibble is reserved for future use.
 *
 * \param sbp_lock_time SBP lock time
 * \return Minimum possible lock time [s]
 */
double decode_lock_time(u8 sbp_lock_time) {
    /* MSB nibble is reserved */
    sbp_lock_time &= 0x0F;

    u32 ms_lock_time;
    if (sbp_lock_time == 0) {
        ms_lock_time = 0;
    } else {
        ms_lock_time = 1u << (sbp_lock_time + 4);
    }

    /* Convert to seconds */
    return (double)ms_lock_time / SECS_MS;
}

void rtcm3_obs_to_sbp( const rtcm_obs_message *rtcm_obs, msg_obs_t *sbp_obs ) {
    //sbp_obs->header.t.wn = thisweek;
    sbp_obs->header.t.tow = rtcm_obs->header.tow;
    sbp_obs->header.t.ns = 0.0;

    u8 index = 0;
    for( u8 sat = 0; sat < rtcm_obs->header.n_sat; ++sat ) {
        for( u8 freq = 0; freq < NUM_FREQS; ++freq ) {
            packed_obs_content_t *sbp_freq = &sbp_obs->obs[index];
            const rtcm_freq_data *rtcm_freq = &rtcm_obs->sats[sat].obs[freq];
            if( rtcm_freq->flags.valid_pr == 1 || rtcm_freq->flags.valid_cp == 1
                || rtcm_freq->flags.valid_cnr == 1 || rtcm_freq->flags.valid_lock == 1 ) {
                sbp_freq->sid.sat = rtcm_obs->sats[sat].svId;
                sbp_freq->sid.code = rtcm_freq->code;
                sbp_freq->flags = 0;
                sbp_freq->P = 0.0;
                sbp_freq->L.i = 0;
                sbp_freq->L.f = 0.0;
                sbp_freq->cn0 = 0.0;
                sbp_freq->lock = 0.0;
                sbp_freq->sid.code = freq;
                if( rtcm_freq->flags.valid_pr == 1 ) {
                    sbp_freq->P = rtcm_freq->pseudorange * MSG_OBS_P_MULTIPLIER;
                    sbp_freq->flags |= NAV_MEAS_FLAG_CODE_VALID;
                }
                if( rtcm_freq->flags.valid_cp == 1 ) {
                    sbp_freq->L.i = (s32)rtcm_freq->carrier_phase;
                    sbp_freq->L.f = (u8)((rtcm_freq->carrier_phase-sbp_freq->L.i)*MSG_OBS_LF_MULTIPLIER);
                    // really???
                    sbp_freq->L.i = -sbp_freq->L.i;
                    sbp_freq->L.f = -sbp_freq->L.f;
                    sbp_freq->flags |= NAV_MEAS_FLAG_PHASE_VALID;
                }
                if( rtcm_freq->flags.valid_cnr == 1 ) {
                    sbp_freq->cn0 = rtcm_freq->cnr * MSG_OBS_CN0_MULTIPLIER;
                    sbp_freq->flags |= NAV_MEAS_FLAG_CN0_VALID;
                }
                if( rtcm_freq->flags.valid_lock == 1 ) {
                    sbp_freq->lock = encode_lock_time(rtcm_freq->lock);
                    // not necessary???
                    //sbp_freq->flags += 1;
                }
                ++index;
            }
        }
    }
    sbp_obs->header.n_obs = index;
}

void sbp_to_rtcm3_obs( const msg_obs_t *sbp_obs, rtcm_obs_message *rtcm_obs ) {
    rtcm_obs->header.tow = sbp_obs->header.t.tow;

    u8 count = 0;
    s8 sat2index[32] = {-1};
    for( u8 obs = 0; obs < sbp_obs->header.n_obs; ++obs ) {
        const packed_obs_content_t *sbp_freq = &sbp_obs->obs[obs];
        if( sbp_freq->flags != 0 ) {
            s8 sat_idx = sat2index[sbp_freq->sid.sat];
            if( sat_idx == -1 ) {
                rtcm_obs->sats[sat_idx].svId = sbp_freq->sid.sat;
                sat_idx = sat2index[sbp_freq->sid.sat] = count++;
            }
            // freq <-> code???
            rtcm_freq_data *rtcm_freq = &rtcm_obs->sats[sat_idx].obs[sbp_freq->sid.code];
            rtcm_freq->flags.data = 0;
            rtcm_freq->code = sbp_freq->sid.code;
            rtcm_freq->pseudorange = 0.0;
            rtcm_freq->carrier_phase = 0.0;
            rtcm_freq->cnr = 0.0;
            rtcm_freq->lock = 0.0;

            if( sbp_freq->flags & NAV_MEAS_FLAG_CODE_VALID ) {
                rtcm_freq->pseudorange = sbp_freq->P / MSG_OBS_P_MULTIPLIER;
                rtcm_freq->flags.valid_pr = 1;
            }
            if( sbp_freq->flags & NAV_MEAS_FLAG_PHASE_VALID ) {
                rtcm_freq->carrier_phase = -( sbp_freq->L.i + (double)sbp_freq->L.f / MSG_OBS_LF_MULTIPLIER );
                rtcm_freq->flags.valid_cp = 1;
            }
            if( sbp_freq->flags & NAV_MEAS_FLAG_CN0_VALID ) {
                rtcm_freq->cnr = sbp_freq->cn0 / MSG_OBS_CN0_MULTIPLIER;
                rtcm_freq->flags.valid_cnr = 1;
            }
            // not necessary???
//            if( sbp_freq->flags == 1 ) {
                rtcm_freq->lock = decode_lock_time(sbp_freq->lock);
                rtcm_freq->flags.valid_lock = 1;
//            }
        }
    }
    rtcm_obs->header.n_sat = count;
}

void rtcm3_1005_to_sbp( const rtcm_msg_1005 *rtcm_1005, msg_base_pos_ecef_t *sbp_base_pos ) {
    sbp_base_pos->x = rtcm_1005->arp_x;
    sbp_base_pos->y = rtcm_1005->arp_y;
    sbp_base_pos->z = rtcm_1005->arp_z;
}

void sbp_to_rtcm3_1005( const msg_base_pos_ecef_t *sbp_base_pos, rtcm_msg_1005 *rtcm_1005 ) {
    rtcm_1005->arp_x = sbp_base_pos->x;
    rtcm_1005->arp_y = sbp_base_pos->y;
    rtcm_1005->arp_z = sbp_base_pos->z;
}

void rtcm3_1006_to_sbp( const rtcm_msg_1006 *rtcm_1006, msg_base_pos_ecef_t *sbp_base_pos ) {
    double llh[3], xyz[3] = { rtcm_1006->msg_1005.arp_x, rtcm_1006->msg_1005.arp_y, rtcm_1006->msg_1005.arp_z };
    wgsecef2llh( xyz, llh );
    llh[2] += rtcm_1006->ant_height;
    wgsllh2ecef( llh, xyz );
    sbp_base_pos->x = xyz[0];
    sbp_base_pos->y = xyz[1];
    sbp_base_pos->z = xyz[2];
}

void sbp_to_rtcm3_1006( const msg_base_pos_ecef_t *sbp_base_pos, rtcm_msg_1006 *rtcm_1006 ) {
    rtcm_1006->msg_1005.arp_x = sbp_base_pos->x;
    rtcm_1006->msg_1005.arp_y = sbp_base_pos->y;
    rtcm_1006->msg_1005.arp_z = sbp_base_pos->z;
    rtcm_1006->ant_height = 0.0;
}

/** Converts from WGS84 geodetic coordinates (latitude, longitude and height)
 * into WGS84 Earth Centered, Earth Fixed Cartesian (ECEF) coordinates
 * (X, Y and Z).
 *
 * Conversion from geodetic coordinates latitude, longitude and height
 * \f$(\phi, \lambda, h)\f$ into Cartesian coordinates \f$(X, Y, Z)\f$ can be
 * achieved with the following formulae:
 *
 * \f[ X = (N(\phi) + h) \cos{\phi}\cos{\lambda} \f]
 * \f[ Y = (N(\phi) + h) \cos{\phi}\sin{\lambda} \f]
 * \f[ Z = \left[(1-e^2)N(\phi) + h\right] \sin{\phi} \f]
 *
 * Where the 'radius of curvature', \f$ N(\phi) \f$, is defined as:
 *
 * \f[ N(\phi) = \frac{a}{\sqrt{1-e^2\sin^2 \phi}} \f]
 *
 * and \f$ a \f$ is the WGS84 semi-major axis and \f$ e \f$ is the WGS84
 * eccentricity. See \ref WGS84_params.
 *
 * \param llh  Geodetic coordinates to be converted, passed as
 *             [lat, lon, height] in [radians, radians, meters].
 * \param ecef Converted Cartesian coordinates are written into this array
 *             as [X, Y, Z], all in meters.
 */
void wgsllh2ecef(const double llh[3], double ecef[3]) {
    double d = WGS84_E * sin(llh[0]);
    double N = WGS84_A / sqrt(1. - d*d);

    ecef[0] = (N + llh[2]) * cos(llh[0]) * cos(llh[1]);
    ecef[1] = (N + llh[2]) * cos(llh[0]) * sin(llh[1]);
    ecef[2] = ((1 - WGS84_E*WGS84_E)*N + llh[2]) * sin(llh[0]);
}

/** Converts from WGS84 Earth Centered, Earth Fixed (ECEF) Cartesian
 * coordinates (X, Y and Z) into WGS84 geodetic coordinates (latitude,
 * longitude and height).
 *
 * Conversion from Cartesian to geodetic coordinates is a much harder problem
 * than conversion from geodetic to Cartesian. There is no satisfactory closed
 * form solution but many different iterative approaches exist.
 *
 * Here we implement a relatively new algorithm due to Fukushima (2006) that is
 * very computationally efficient, not requiring any transcendental function
 * calls during iteration and very few divisions. It also exhibits cubic
 * convergence rates compared to the quadratic rate of convergence seen with
 * the more common algortihms based on the Newton-Raphson method.
 *
 * References:
 *   -# "A comparison of methods used in rectangular to Geodetic Coordinates
 *      Transformations", Burtch R. R. (2006), American Congress for Surveying
 *      and Mapping Annual Conference. Orlando, Florida.
 *   -# "Transformation from Cartesian to Geodetic Coordinates Accelerated by
 *      Halley’s Method", T. Fukushima (2006), Journal of Geodesy.
 *
 * \param ecef Cartesian coordinates to be converted, passed as [X, Y, Z],
 *             all in meters.
 * \param llh  Converted geodetic coordinates are written into this array as
 *             [lat, lon, height] in [radians, radians, meters].
 */
void wgsecef2llh(const double ecef[3], double llh[3]) {
    /* Distance from polar axis. */
    const double p = sqrt(ecef[0]*ecef[0] + ecef[1]*ecef[1]);

    /* Compute longitude first, this can be done exactly. */
    if (p != 0)
        llh[1] = atan2(ecef[1], ecef[0]);
    else
        llh[1] = 0;

    /* If we are close to the pole then convergence is very slow, treat this is a
     * special case. */
    if (p < WGS84_A*1e-16) {
        llh[0] = copysign(M_PI_2, ecef[2]);
        llh[2] = fabs(ecef[2]) - WGS84_B;
        return;
    }

    /* Caluclate some other constants as defined in the Fukushima paper. */
    const double P = p / WGS84_A;
    const double e_c = sqrt(1. - WGS84_E*WGS84_E);
    const double Z = fabs(ecef[2]) * e_c / WGS84_A;

    /* Initial values for S and C correspond to a zero height solution. */
    double S = Z;
    double C = e_c * P;

    /* Neither S nor C can be negative on the first iteration so
     * starting prev = -1 will not cause and early exit. */
    double prev_C = -1;
    double prev_S = -1;

    double A_n, B_n, D_n, F_n;

    /* Iterate a maximum of 10 times. This should be way more than enough for all
     * sane inputs */
    for (int i=0; i<10; i++)
    {
        /* Calculate some intermmediate variables used in the update step based on
         * the current state. */
        A_n = sqrt(S*S + C*C);
        D_n = Z*A_n*A_n*A_n + WGS84_E*WGS84_E*S*S*S;
        F_n = P*A_n*A_n*A_n - WGS84_E*WGS84_E*C*C*C;
        B_n = 1.5*WGS84_E*S*C*C*(A_n*(P*S - Z*C) - WGS84_E*S*C);

        /* Update step. */
        S = D_n*F_n - B_n*S;
        C = F_n*F_n - B_n*C;

        /* The original algorithm as presented in the paper by Fukushima has a
         * problem with numerical stability. S and C can grow very large or small
         * and over or underflow a double. In the paper this is acknowledged and
         * the proposed resolution is to non-dimensionalise the equations for S and
         * C. However, this does not completely solve the problem. The author caps
         * the solution to only a couple of iterations and in this period over or
         * underflow is unlikely but as we require a bit more precision and hence
         * more iterations so this is still a concern for us.
         *
         * As the only thing that is important is the ratio T = S/C, my solution is
         * to divide both S and C by either S or C. The scaling is chosen such that
         * one of S or C is scaled to unity whilst the other is scaled to a value
         * less than one. By dividing by the larger of S or C we ensure that we do
         * not divide by zero as only one of S or C should ever be zero.
         *
         * This incurs an extra division each iteration which the author was
         * explicityl trying to avoid and it may be that this solution is just
         * reverting back to the method of iterating on T directly, perhaps this
         * bears more thought?
         */

        if (S > C) {
            C = C / S;
            S = 1;
        } else {
            S = S / C;
            C = 1;
        }

        /* Check for convergence and exit early if we have converged. */
        if (fabs(S - prev_S) < 1e-16 && fabs(C - prev_C) < 1e-16) {
            break;
        } else {
            prev_S = S;
            prev_C = C;
        }
    }

    A_n = sqrt(S*S + C*C);
    llh[0] = copysign(1.0, ecef[2]) * atan(S / (e_c*C));
    llh[2] = (p*e_c*C + fabs(ecef[2])*S - WGS84_A*e_c*A_n) / sqrt(e_c*e_c*C*C + S*S);
}