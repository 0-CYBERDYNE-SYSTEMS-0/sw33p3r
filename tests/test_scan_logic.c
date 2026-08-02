/* Host tests: Marauder timeout decision + haversine distance. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "room_sweep_scan.h"

static int fails;

static void expect_true(const char* name, bool cond) {
    if(cond) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        fails++;
    }
}

static void expect_near(const char* name, float got, float want, float tol) {
    if(fabsf(got - want) <= tol) {
        printf("PASS: %s (%.2f ≈ %.2f)\n", name, (double)got, (double)want);
    } else {
        printf("FAIL: %s got=%.2f want=%.2f tol=%.2f\n",
               name, (double)got, (double)want, (double)tol);
        fails++;
    }
}

int main(void) {
    fails = 0;

    /* Timeout: not scanning */
    expect_true(
        "idle never errors",
        !marauder_scan_should_error(false, 100000, 0, 30000, 0));

    /* CRITICAL: last_data_tick==0 style — scan just started, zero results, now large */
    expect_true(
        "fresh scan does not instant-error",
        !marauder_scan_should_error(true, 100000, 99950, 30000, 0));

    /* Zero results for full 30s → error */
    expect_true(
        "zero results after timeout errors",
        marauder_scan_should_error(true, 100000, 70000, 30000, 0));

    /* Results exist → silence is OK even past 30s */
    expect_true(
        "results present: silence is not error",
        !marauder_scan_should_error(true, 100000, 1000, 30000, 3));

    /* Boundary: exactly timeout */
    expect_true(
        "exactly at timeout with zero results",
        marauder_scan_should_error(true, 30000, 0, 30000, 0));

    expect_true(
        "one ms under timeout with zero results",
        !marauder_scan_should_error(true, 29999, 0, 30000, 0));

    /* Haversine: ~111.2 km per degree latitude at equator-ish */
    float d_1deg = geo_distance_m(0.0f, 0.0f, 1.0f, 0.0f);
    expect_near("1 deg lat ~111.2km", d_1deg, 111195.0f, 500.0f);

    float d_same = geo_distance_m(37.7749f, -122.4194f, 37.7749f, -122.4194f);
    expect_near("same point is 0m", d_same, 0.0f, 0.5f);

    /* Short hop ~1km south */
    float d_short = geo_distance_m(0.0f, 0.0f, 0.009f, 0.0f);
    expect_near("~1km hop", d_short, 1000.0f, 30.0f);

    printf("RESULT: %s (%d failure(s))\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
