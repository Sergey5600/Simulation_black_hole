// Shared helpers, prepended to shaders that ask for them (see Renderer.cpp).
// Enum values mirror src/sim/Appearance.hpp.

#define STYLE_BLACKBODY 0
#define STYLE_COLORMAP 1

#define Q_TEMPERATURE 0
#define Q_RADIUS 1
#define Q_SPEED 2
#define Q_REDSHIFT 3
#define Q_TIME_DILATION 4
#define Q_AGE 5
#define Q_IMPACT 6
#define Q_TAG 7

#define CM_NEBULA 0
#define CM_INFERNO 1
#define CM_MAGMA 2
#define CM_PLASMA 3
#define CM_VIRIDIS 4
#define CM_TURBO 5
#define CM_ICE 6

// Thin-disk (Novikov-Thorne / Shakura-Sunyaev, zero torque at the ISCO = 6M)
// effective temperature, normalized to 1 at its peak r = 49/6 M:
//   T^4 ∝ r^-3 (1 - sqrt(6M / r)).
// Inside the ISCO the profile is zero; plunging gas gets a fading glow instead
// (an approximation: real plunging gas keeps some heat).
float diskTemperature(float r) {
    const float kPeakT4 = 2.62282e-4;  // (6/49)^3 / 7
    float t4 = pow(r, -3.0) * max(1.0 - sqrt(6.0 / r), 0.0);
    float t = pow(t4 / kPeakT4, 0.25);
    float plunge = 0.5 * clamp((r - 2.0) / 4.0, 0.0, 1.0);
    return max(t, plunge);
}

// Color of a black body (Tanner Helland's fit of the Planckian locus, 1000-40000 K),
// returned as linear RGB with the brightest channel at 1.
vec3 blackbody(float kelvin) {
    float t = clamp(kelvin, 1000.0, 40000.0) / 100.0;
    vec3 c;
    c.r = t <= 66.0 ? 1.0 : clamp(1.29293618606 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);
    c.g = t <= 66.0 ? clamp(0.39008157876 * log(t) - 0.63184144378, 0.0, 1.0)
                    : clamp(1.12989086089 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);
    c.b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : clamp(0.54320678911 * log(t - 10.0) - 1.19625408914, 0.0, 1.0));
    return pow(c, vec3(2.2));
}

// Polynomial fits of the matplotlib maps (Matt Zucker, CC0) and of Turbo (Google, Apache 2.0).
vec3 polyMap(float t, vec3 c0, vec3 c1, vec3 c2, vec3 c3, vec3 c4, vec3 c5, vec3 c6) {
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

vec3 viridis(float t) {
    return polyMap(t, vec3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061),
                   vec3(0.1050930431085774, 1.404613529898575, 1.384590162594685),
                   vec3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659),
                   vec3(-4.634230498983486, -5.799100973351585, -19.33244095627987),
                   vec3(6.228269936347081, 14.17993336680509, 56.69055260068105),
                   vec3(4.776384997670288, -13.74514537774601, -65.35303263337234),
                   vec3(-5.435455855934631, 4.645852612178535, 26.3124352495832));
}

vec3 inferno(float t) {
    return polyMap(t, vec3(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184),
                   vec3(0.1065134194856116, 0.5639564367884091, 3.932712388889277),
                   vec3(11.60249308247187, -3.972853965665698, -15.9423941062914),
                   vec3(-41.70399613139459, 17.43639888205313, 44.35414519872813),
                   vec3(77.162935699427, -33.40235894210092, -81.80730925738993),
                   vec3(-71.31942824499214, 32.62606426397723, 73.20951985803202),
                   vec3(25.13112622477341, -12.24266895238567, -23.07032500287172));
}

vec3 magma(float t) {
    return polyMap(t, vec3(-0.002136485053939582, -0.000749655052795221, -0.005386127855323933),
                   vec3(0.2516605407371642, 0.6775232436837668, 2.494026599312351),
                   vec3(8.353717279216625, -3.577719514958484, 0.3144679030132573),
                   vec3(-27.66873308576866, 14.26473078096533, -13.64921318813922),
                   vec3(52.17613981234068, -27.94360607168351, 12.94416944238394),
                   vec3(-50.76852536473588, 29.04658282127291, 4.23415299384598),
                   vec3(18.65570506591883, -11.48977351997711, -5.601961508734096));
}

vec3 plasma(float t) {
    return polyMap(t, vec3(0.05873234392399702, 0.02333670892565664, 0.5433401826748754),
                   vec3(2.176514634195958, 0.2383834171260182, 0.7539604599784036),
                   vec3(-2.689460476458034, -7.455851135738909, 3.110799939717086),
                   vec3(6.130348345893603, 42.3461881477227, -28.51885465332158),
                   vec3(-11.10743619062271, -82.66631109428045, 60.13984767418263),
                   vec3(10.02306557647065, 71.41361770095349, -54.07218655560067),
                   vec3(-3.658713842777788, -22.93153465461149, 18.19190778539828));
}

vec3 turbo(float x) {
    const vec4 kRed4 = vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    const vec4 kGreen4 = vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
    const vec4 kBlue4 = vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    const vec2 kRed2 = vec2(-152.94239396, 59.28637943);
    const vec2 kGreen2 = vec2(4.27729857, 2.82956604);
    const vec2 kBlue2 = vec2(-89.90310912, 27.34824973);
    vec4 v4 = vec4(1.0, x, x * x, x * x * x);
    vec2 v2 = v4.zw * v4.z;
    return vec3(dot(v4, kRed4) + dot(v2, kRed2), dot(v4, kGreen4) + dot(v2, kGreen2),
                dot(v4, kBlue4) + dot(v2, kBlue2));
}

// Piecewise-linear gradient through 6 stops placed at 0, .2, .4, .6, .8, 1.
vec3 gradient6(float t, vec3 a, vec3 b, vec3 c, vec3 d, vec3 e, vec3 f) {
    float s = t * 5.0;
    if (s < 1.0) return mix(a, b, s);
    if (s < 2.0) return mix(b, c, s - 1.0);
    if (s < 3.0) return mix(c, d, s - 2.0);
    if (s < 4.0) return mix(d, e, s - 3.0);
    return mix(e, f, s - 4.0);
}

// Deep violet -> magenta -> coral -> warm white (the "atom cloud" look).
vec3 nebula(float t) {
    return gradient6(t, vec3(0.03, 0.01, 0.10), vec3(0.24, 0.05, 0.48), vec3(0.58, 0.12, 0.78),
                     vec3(0.95, 0.30, 0.62), vec3(1.00, 0.62, 0.36), vec3(1.00, 0.96, 0.88));
}

vec3 ice(float t) {
    return gradient6(t, vec3(0.01, 0.02, 0.06), vec3(0.05, 0.12, 0.35), vec3(0.10, 0.35, 0.75),
                     vec3(0.25, 0.65, 0.95), vec3(0.65, 0.88, 1.00), vec3(0.97, 0.99, 1.00));
}

// Returns linear RGB (the polynomial maps are defined in sRGB, so convert).
vec3 colormap(int id, float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    if (id == CM_NEBULA) c = nebula(t);
    else if (id == CM_INFERNO) c = inferno(t);
    else if (id == CM_MAGMA) c = magma(t);
    else if (id == CM_PLASMA) c = plasma(t);
    else if (id == CM_VIRIDIS) c = viridis(t);
    else if (id == CM_TURBO) c = turbo(t);
    else c = ice(t);
    return pow(clamp(c, 0.0, 1.0), vec3(2.2));
}
