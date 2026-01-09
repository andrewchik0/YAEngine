//
// https://www.shadertoy.com/view/tt3fRX#
//
// ------------------------------------------------------------------------- //

// CONFIG

#define EMA_IIR_INVERSE_CUTOFF_FREQUENCY        (0.2)      // 0.0 - 0.999
#define VARIANCE_CLIPPING_COLOR_BOX_SIGMA       (0.975)      // 0.5 - 1.0

// ------------------------------------------------------------------------- //

#define SAMPLE_RGBA(sampler, coord) (texture((sampler), (coord)))
#define SAMPLE_RGB(sampler, coord) (SAMPLE_RGBA((sampler), (coord)).rgb)

// ------------------------------------------------------------------------- //

// https://www.shadertoy.com/view/4djSRW
vec3 hash33(vec3 p3) {
	p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

// ------------------------------------------------------------------------- //

// https://www.shadertoy.com/view/4dSBDt

vec3 RGBToYCoCg(vec3 RGB) {
    float cTerm = 0.5 * 256.0 / 255.0;
	float Y  = dot(RGB, vec3( 1, 2,  1)) * 0.25;
	float Co = dot(RGB, vec3( 2, 0, -2)) * 0.25 + cTerm;
	float Cg = dot(RGB, vec3(-1, 2, -1)) * 0.25 + cTerm;
	return vec3(Y, Co, Cg);
}

vec3 YCoCgToRGB(vec3 YCoCg) {
	float cTerm = 0.5 * 256.0 / 255.0;
	float Y  = YCoCg.x;
	float Co = YCoCg.y - cTerm;
	float Cg = YCoCg.z - cTerm;
	float R  = Y + Co - Cg;
	float G  = Y + Cg;
	float B  = Y - Co - Cg;
	return vec3(R, G, B);
}

// ------------------------------------------------------------------------- //

// based on https://www.shadertoy.com/view/4dSBDt
void getVarianceClippingBounds(vec3 color, sampler2D colorSampler, ivec2 screenSpaceUV, float colorBoxSigma, out vec3 colorMin, out vec3 colorMax) {
    vec3 colorAvg = color;
    vec3 colorVar = color * color;

    // Marco Salvi's Implementation (by Chris Wyman)
    // unrolled loop version

    vec3 fetch = vec3(0);

    // unwinded the for loop
    {
        // top
        {
            // left / top
            fetch = texelFetch(colorSampler, screenSpaceUV + ivec2(-1, -1), 0).rgb;
            fetch = RGBToYCoCg(fetch);
            colorAvg += fetch;
            colorVar += fetch * fetch;

            // center / top
            fetch = texelFetch(colorSampler, screenSpaceUV + ivec2( 0, -1), 0).rgb;
            fetch = RGBToYCoCg(fetch);
            colorAvg += fetch;
            colorVar += fetch * fetch;

            // right / top
            fetch = texelFetch(colorSampler, screenSpaceUV + ivec2( 1, -1), 0).rgb;
            fetch = RGBToYCoCg(fetch);
            colorAvg += fetch;
            colorVar += fetch * fetch;
        }

        // center
        {
            // left / center
            fetch = texelFetch(colorSampler, screenSpaceUV + ivec2(-1,  0), 0).rgb;
            fetch = RGBToYCoCg(fetch);
            colorAvg += fetch;
            colorVar += fetch * fetch;


            // center / center is intentionally skipped


            // right / center
            fetch = texelFetch(colorSampler, screenSpaceUV + ivec2( 1,  0), 0).rgb;
            fetch = RGBToYCoCg(fetch);
            colorAvg += fetch;
            colorVar += fetch * fetch;
        }

        // bottom
        {
            // left / bottom
            fetch = texelFetch(colorSampler, screenSpaceUV + ivec2(-1,  1), 0).rgb;
            fetch = RGBToYCoCg(fetch);
            colorAvg += fetch;
            colorVar += fetch * fetch;

            // center / bottom
            fetch = texelFetch(colorSampler, screenSpaceUV + ivec2( 0,  1), 0).rgb;
            fetch = RGBToYCoCg(fetch);
            colorAvg += fetch;
            colorVar += fetch * fetch;

            // right / bottom
            fetch = texelFetch(colorSampler, screenSpaceUV + ivec2( 1,  1), 0).rgb;
            fetch = RGBToYCoCg(fetch);
            colorAvg += fetch;
            colorVar += fetch * fetch;
        }
    }

    colorAvg *= 0.111111111;
    colorVar *= 0.111111111;

    vec3 sigma = sqrt(max(vec3(0.0), colorVar - colorAvg * colorAvg));
	colorMin = colorAvg - colorBoxSigma * sigma;
	colorMax = colorAvg + colorBoxSigma * sigma;
}
