@@Version

// Global
@@Uniform(float VIEW_DISTANCE);
@@Uniform(float GLOBAL_TIME);
@@Uniform(float PIXEL_SIZE);

// Per batch
@@Uniform(int MATERIAL_INDEX);
@@Uniform(float MATERIAL_PARAMS[8]);
## Texture
@@Texture(sampler2D TEX1);
##

#define CLOUD_SCALE      0.0035
#define CLOUD_DETAIL     2.0
#define CLOUD_DENSITY    0.55
#define CLOUD_COVERAGE   0.45

float hash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// 3D value noise
float noise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);

    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash(i + vec3(0,0,0));
    float n100 = hash(i + vec3(1,0,0));
    float n010 = hash(i + vec3(0,1,0));
    float n110 = hash(i + vec3(1,1,0));

    float n001 = hash(i + vec3(0,0,1));
    float n101 = hash(i + vec3(1,0,1));
    float n011 = hash(i + vec3(0,1,1));
    float n111 = hash(i + vec3(1,1,1));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);

    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);

    return mix(nxy0, nxy1, f.z);
}

// Fractal Brownian Motion
float fbm(vec3 p)
{
    float v = 0.0;
    float a = 0.5;

    for(int i = 0; i < 5; i++)
    {
        v += noise(p) * a;
        p *= 2.0;
        a *= 0.5;
    }

    return v;
}

//
// Marble
//
// Vertex colour:
// - 
//
// Params:
//   0: p_base_scale:
//   1: p_veins_scale:
//   2: p_medium_scale:
//   3: p_stone_mix
vec3 stoneTexture(vec3 p)
{
    // -------------------------------------------------
    // Parameter vars
    // -------------------------------------------------
	float p_base_scale = @Uniform(MATERIAL_PARAMS[0]);
	float p_medium_scale = @Uniform(MATERIAL_PARAMS[1]);
	float p_stone_mix = @Uniform(MATERIAL_PARAMS[2]);
	
    // Large-scale stone structure
    float base = fbm(p * p_base_scale);

    // Medium detail
    float detail = fbm(p * p_medium_scale);

    // Crack-like veins
    float veins = abs(noise(p * 16.0) - 0.5);
    veins = smoothstep(0.15, 0.0, veins);

    // Combine layers
    float stone = base * p_stone_mix + detail * (1.0 - p_stone_mix);

    // Base rock colors
    //vec3 darkRock  = vec3(0.25, 0.24, 0.22);
	vec3 darkRock = @In(COLOUR).xyz;
    vec3 lightRock = vec3(0.55, 0.53, 0.50);

    vec3 color = mix(darkRock, lightRock, stone);

    // Add dark veins/cracks
    color *= 1.0 - veins * 0.5;

    // Slight color variation
    color += vec3(
        noise(p * 4.1),
        noise(p * 4.3),
        noise(p * 4.7)
    ) * 0.05;

    return color;
}

//
// Marble
//
// Vertex colour:
// - 
//
// Params:
//   0: p_warp_scale:
//   1: p_veins_scale:
//   2: p_veins_fine_scale:
//   3: p_fine_detail_scale:
//   4: p_light_warm_mix:
//   5: p_vein_mix:
//   6: p_cloudiness:
//   7: p_fbm_scale:
vec3 marbleTexture(vec3 p)
{
    // -------------------------------------------------
    // Parameter vars
    // -------------------------------------------------
	float p_warp_scale = @Uniform(MATERIAL_PARAMS[0]);
	float p_veins_scale = @Uniform(MATERIAL_PARAMS[1]);
	float p_veins_fine_scale = @Uniform(MATERIAL_PARAMS[2]);
	float p_fine_detail_scale = @Uniform(MATERIAL_PARAMS[3]);
	float p_light_warm_mix = @Uniform(MATERIAL_PARAMS[4]);
	float p_vein_mix = @Uniform(MATERIAL_PARAMS[5]);
	float p_cloudiness = @Uniform(MATERIAL_PARAMS[6]);
	float p_fbm_scale = @Uniform(MATERIAL_PARAMS[7]);
	
    // -------------------------------------------------
    // Domain warping for flowing veins
    // -------------------------------------------------
    vec3 warp;
    warp.x = fbm(p * p_fbm_scale * 4.0 + vec3(1.7, 9.2, 2.4));
    warp.y = fbm(p * p_fbm_scale * 4.0 + vec3(8.3, 2.8, 5.1));
    warp.z = fbm(p * p_fbm_scale * 4.0 + vec3(4.5, 1.3, 7.2));

    p += warp * p_warp_scale;

    // -------------------------------------------------
    // Main marble veins
    // -------------------------------------------------
    float veins =
        sin(p.x * p_veins_scale +
            fbm(p * p_fbm_scale * 6.0) * (p_veins_scale * 0.8333));

    veins = veins * 0.5 + 0.5;

    // Sharpen veins
    veins = smoothstep(0.35, 0.85, veins);

    // -------------------------------------------------
    // Fine detail veins
    // -------------------------------------------------
    float fine =
        sin(p.x * p_veins_fine_scale +
            fbm(p * p_fbm_scale * 20.0) * (p_veins_fine_scale / 6.0));

    fine = fine * 0.5 + 0.5;
    fine *= p_fine_detail_scale;

    // -------------------------------------------------
    // Marble base variation
    // -------------------------------------------------
    float base = fbm(p * p_fbm_scale * 2.4);

    // -------------------------------------------------
    // Marble colors
    // -------------------------------------------------
    //vec3 darkVein = vec3(0.18, 0.18, 0.20);
	vec3 darkVein = @In(COLOUR).xyz;
    vec3 lightMarble = vec3(0.92, 0.92, 0.95);
    vec3 warmTint = vec3(0.96, 0.94, 0.90);
    
    // Black
    //vec3 darkVein    = vec3(0.85, 0.85, 0.90);
    //vec3 lightMarble = vec3(0.08, 0.08, 0.10);
    //vec3 warmTint    = vec3(0.12, 0.12, 0.14);    

    // Emerald
    //vec3 darkVein    = vec3(0.02, 0.08, 0.04);
    //vec3 lightMarble = vec3(0.45, 0.60, 0.50);
    //vec3 warmTint    = vec3(0.25, 0.40, 0.32);    

    // Base stone
    vec3 color = mix(lightMarble, warmTint, base * p_light_warm_mix);

    // Main veins
    color = mix(color, darkVein, veins * p_vein_mix);

    // Fine veins
    color -= fine;

    // Soft cloudy variation
    color += (fbm(p * p_fbm_scale * 16.0) - 0.5) * mix(0.0, 0.2, p_cloudiness);

    return color;
}

float cloudDensity(vec3 worldPos)
{
    // Scale world space into cloud space
    vec3 p = worldPos * CLOUD_SCALE;

    // Large cloud shapes
    float base = fbm(p);

    // Fine detail
    float detail = fbm(p * 4.0) * 0.25;

    float d = base + detail;

    // Shape clouds
    d = smoothstep(CLOUD_COVERAGE,
                   CLOUD_COVERAGE + CLOUD_DENSITY,
                   d);

    return clamp(d, 0.0, 1.0);
}

/////// Crystal test
// ----------------------------------------------------
// Hash functions
// ----------------------------------------------------

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

// ----------------------------------------------------
// 3D Voronoi distance
// ----------------------------------------------------

float crystalVoronoi(vec3 p)
{
    vec3 cell = floor(p);
    vec3 local = fract(p);

    float minDist = 1000.0;

    for(int z=-1; z<=1; z++)
    for(int y=-1; y<=1; y++)
    for(int x=-1; x<=1; x++)
    {
        vec3 offset = vec3(x,y,z);

        vec3 feature =
            offset +
            hash33(cell + offset);

        vec3 diff = feature - local;

        minDist = min(minDist, length(diff));
    }

    return minDist;
}

// ----------------------------------------------------
// Fractal crystal field
// ----------------------------------------------------

float crystalField(vec3 wp)
{
    float f = 0.0;

    f += crystalVoronoi(wp * 2.0) * 0.7;
    f += crystalVoronoi(wp * 4.0) * 0.2;
    f += crystalVoronoi(wp * 8.0) * 0.1;

    return f;
}

// ----------------------------------------------------
// Crystal material generation
// ----------------------------------------------------

struct CrystalMaterial
{
    vec3 albedo;
    float roughness;
    float metallic;
};

CrystalMaterial getCrystalMaterial(vec3 worldPos)
{
    CrystalMaterial m;

    //--------------------------------------------------
    // World position drives everything
    //--------------------------------------------------

    vec3 p = worldPos * 0.35;

    //--------------------------------------------------
    // Main crystal cells
    //--------------------------------------------------

    float cell = crystalField(p);

    //--------------------------------------------------
    // Faceted bands
    //--------------------------------------------------

    float facets =
        abs(dot(normalize(vec3(1,1,0)), p)) +
        abs(dot(normalize(vec3(-1,1,1)), p)) +
        abs(dot(normalize(vec3(1,0,-1)), p));

    facets = fract(facets * 2.0);

    //--------------------------------------------------
    // Internal growth striations
    //--------------------------------------------------

    float growth =
        sin(
            p.y * 40.0 +
            p.x * 8.0 +
            p.z * 5.0
        );

    growth = growth * 0.5 + 0.5;

    //--------------------------------------------------
    // Crystal edge mask
    //--------------------------------------------------

    float edges =
        smoothstep(
            0.05,
            0.25,
            cell
        );

    //--------------------------------------------------
    // Color palette
    //--------------------------------------------------

    vec3 deepColor =
        vec3(0.05, 0.25, 0.65);

    vec3 midColor =
        vec3(0.25, 0.75, 1.0);

    vec3 brightColor =
        vec3(0.9, 1.0, 1.0);

    vec3 color =
        mix(
            deepColor,
            midColor,
            edges
        );

    color =
        mix(
            color,
            brightColor,
            growth * 0.4
        );

    //--------------------------------------------------
    // Facet highlight
    //--------------------------------------------------

    color += pow(facets, 12.0) * 0.25;

    //--------------------------------------------------
    // Roughness variation
    //--------------------------------------------------

    float roughness =
        mix(
            0.02,
            0.18,
            cell
        );

    roughness *=
        mix(
            0.7,
            1.3,
            growth
        );

    //--------------------------------------------------
    // Final values
    //--------------------------------------------------

    m.albedo = color;
    m.roughness = clamp(roughness, 0.01, 1.0);
    m.metallic = 0.0;

    return m;
}
//////////////////////////

vec3 snapToGrid(vec3 p, float gridSize)
{
	return round(p / gridSize) * gridSize;
}

void main()
{
	// Depth scaling factor
    float depth = gl_FragCoord.z / gl_FragCoord.w;
	
	depth /= @Uniform(VIEW_DISTANCE);
	depth = pow(1 - depth, 1.7);

	// Calculate material value
	//float modt = (sin(@Uniform(GLOBAL_TIME)) * 0.5) + 0.6;
	vec3 clamped = snapToGrid(@In(FRAGPOSITION) / 32.0, @Uniform(PIXEL_SIZE));
	//vec3 clamped = @In(FRAGPOSITION) / 32.0;
	
	vec3 value = vec3(1.0, 1.0, 1.0);
	
	switch (@Uniform(MATERIAL_INDEX)) 
	{
		case 0:
			value = marbleTexture(clamped);
			//value = vec3(0.5, 0.5, 0.5);//@In(COLOUR).xyz;//marbleTexture(clamped);
			break;
			
		case 1:
			value = stoneTexture(clamped);
			//value = vec3(0.5, 0.5, 0.5);//@In(COLOUR).xyz;//stoneTexture(clamped);
			//CrystalMaterial mat = getCrystalMaterial(clamped);
			//value = mat.albedo;
			break;
		
		default:
			value = vec3(1.0, 0.0, 1.0);
			break;
	}

	// Shading
    vec3 normalDir = @In(NORMAL);
    vec3 viewDir = normalize(@ViewPos - @In(FRAGPOSITION));	
	vec4 shadedColour = vec4(depth, depth, depth, 1.0);

	@Out(vec4 COLOUR) = vec4(value, 1.0f) * shadedColour;
    //@Out(vec4 COLOUR) = texture(@Texture(TEX1), @In(TEXCOORDS).xy) * shadedColour;
	//#@Out(vec4 COLOUR) = mix(marble, stone, cloudDensity(clamped)) * shadedColour;
##
}
