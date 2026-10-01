// RE Engine surface for Qt Quick 3D custom materials (see ReMaterial.qml)
// Colour textures are sRGB GPU textures (decoded by the sampler), so values here are already linear.

// Terrain: the mask is a map-wide index map (UV0 spans the whole map), each texel names a ground layer; the base
// and normal maps are grids of those layers (atlasColumns wide), tiled every groundTile metres. The four nearest
// index texels are blended; explicit gradients keep the mip level of the tiling, not of the cell jumps.
void terrainSample(vec2 uv, vec3 wpos, out vec4 base, out vec4 nr)
{
    vec2 ts = vec2(textureSize(maskMap, 0));
    vec2 p = uv * ts - 0.5;
    vec2 f = fract(p);
    ivec2 i0 = ivec2(floor(p));
    ivec2 last = ivec2(ts) - 1;
    float cols = max(1.0, atlasColumns);
    int ci = int(cols + 0.5);
    vec2 tuv = wpos.xz / max(0.1, groundTile);
    vec2 gx = dFdx(tuv) / cols, gy = dFdy(tuv) / cols;
    float cellTexels = float(textureSize(baseMap, 0).x) / cols;
    vec2 cell = clamp(fract(tuv), vec2(1.0 / cellTexels), vec2(1.0 - 1.0 / cellTexels));
    base = vec4(0.0);
    nr = vec4(0.0);
    for (int k = 0; k < 4; ++k) {
        ivec2 o = ivec2(k & 1, k >> 1);
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
        int layer = int(texelFetch(maskMap, clamp(i0 + o, ivec2(0), last), 0).r * 255.0 / 16.0 + 0.5);
        vec2 auv = (vec2(float(layer % ci), float(layer / ci)) + cell) / cols;
        base += textureGrad(baseMap, auv, gx, gy) * w;
        nr += textureGrad(normalMap, auv, gx, gy) * w;
    }
}

void MAIN()
{
    // world materials tile their maps (baseUv / normalUv: scale xy, offset zw); characters use them 1:1
    vec2 buv = UV0 * baseUv.xy + baseUv.zw;
    vec2 nuv = UV0 * normalUv.xy + normalUv.zw;
    vec4 base, nr;
    if (shading > 1.5 && hasMask > 0.5) terrainSample(UV0, VAR_WORLD_POSITION, base, nr);
    else { base = texture(baseMap, buv); nr = texture(normalMap, nuv); }
    vec3 albedo = (hasBase > 0.5 ? base.rgb : vec3(1.0)) * tint.rgb;

    // layered world surfaces: a second (tiled) layer by mask red, dirt colour by mask blue, occlusion in mask alpha
    float layerOcclusion = 1.0;
    if (shading > 0.5 && shading < 1.5) {
        vec4 mk = hasMask > 0.5 ? texture(maskMap, maskUvSet > 0.5 ? UV1 : UV0) : vec4(0.0, 0.0, 0.0, 1.0);
        if (hasDetail > 0.5)
            albedo = mix(albedo, texture(detailMap, UV0 * detailUv.xy + detailUv.zw).rgb * detailTint.rgb, clamp(mk.r, 0.0, 1.0));
        albedo = mix(albedo, dirtColor.rgb, clamp(mk.b * dirtColor.w * 1.5, 0.0, 1.0));
        layerOcclusion = hasMask > 0.5 ? mk.a : 1.0;
    }

    float metal = metalValue;
    if (baseAlphaMode > 0.5 && baseAlphaMode < 1.5) metal = 1.0 - base.a;
    else if (baseAlphaMode > 1.5 && baseAlphaMode < 2.5) metal = base.a;

    float rough = normalRough > 0.5 ? nr.a : roughnessValue;
    float cavity = 1.0;
    // two-sided surfaces (hair cards, cloth edges) light their back faces from the back
    float side = gl_FrontFacing ? 1.0 : -1.0;
    vec3 wn = VAR_WORLD_NORMAL * side;
    NORMAL = normalize(wn);
    if (hasNormal > 0.5) {
        // NRMR: normal in rgb, roughness in a. NRRC (world surfaces): roughness r, normal y in g, cavity b, normal x in a
        vec2 xy = normalLayout > 0.5 ? vec2(nr.a, nr.g) * 2.0 - 1.0 : nr.xy * 2.0 - 1.0;
        if (normalLayout > 0.5) { rough = nr.r; cavity = nr.b; }
        float z = sqrt(max(0.0, 1.0 - dot(xy, xy)));
        vec3 n = normalize(VAR_WORLD_TANGENT * xy.x * side + VAR_WORLD_BINORMAL * xy.y + wn * z);
        NORMAL = n;
    }

    vec4 pk = texture(packedMap, buv);
    float alpha = 1.0;
    if (hasPacked > 0.5) alpha = pk.r;
    else if (baseAlphaMode > 2.5) alpha = base.a;
    if (hair > 0.5) {
        // strand cards: the game dithers them by their alpha and resolves with TAA, which amounts to blending by
        // the alpha itself - fine flyaway layers stay a faint haze. The opaque cores still write depth (pre-pass).
        alpha = clamp(alpha * 1.1, 0.0, 1.0);
        if (alpha < 0.01) discard;
    } else if (alphaTest > 0.5 && alpha < alphaCutoff) discard;

    if (glass > 0.5) alpha *= tint.a;               // cornea, wet layers, lenses, decals: the game's opacity
    // Solid shading (Blender): every surface a light grey, the shape and its normal-map detail under the light
    if (clay > 0.5) { albedo = vec3(0.34); metal = 0.0; rough = 0.7; cavity = 1.0; }
    BASE_COLOR = vec4(albedo, alpha);
    METALNESS = clamp(metal, 0.0, 1.0);
    // hair: RE lights strands with a shifted anisotropic highlight; an isotropic one on thin cards reads as a pale
    // sheen, so keep it broad and weak
    if (hair > 0.5) rough = max(rough, 0.55);
    ROUGHNESS = clamp(rough, 0.03, 1.0);
    // hair keeps the occlusion of the hair volume in ATOC blue on the second UV set (it shades the ambient light;
    // tinting the strands towards OcclusionColor as the game does came out far darker than the game)
    float occlusion = hasPacked > 0.5 ? pk.b : 1.0;
    if (hair > 0.5 && hasPacked > 0.5) {
        occlusion = texture(packedMap, maskUvSet > 0.5 ? UV1 : UV0).b;
    }
    OCCLUSION_AMOUNT = occlusion * layerOcclusion;
    SPECULAR_AMOUNT = hair > 0.5 ? 0.15 : 0.5 * cavity;
    if (debugView > 0.5) {
        vec3 c = debugView < 1.5 ? base.rgb : debugView < 2.5 ? vec3(base.a) : debugView < 3.5 ? nr.rgb : debugView < 4.5 ? pk.rgb : debugView < 5.5 ? vec3(fract(UV0), 0.0) : tint.rgb;
        BASE_COLOR = vec4(0.0, 0.0, 0.0, 1.0);
        METALNESS = 0.0;
        ROUGHNESS = 1.0;
        EMISSIVE_COLOR = c;
        return;
    }
    if (hasEmissive > 0.5 && clay < 0.5)
        EMISSIVE_COLOR = texture(emissiveMap, buv).rgb * emissiveColor * emissiveIntensity;
    // selection: a thin warm rim along the silhouette, the picture itself stays readable
    if (highlight > 0.0) {
        float ndv = clamp(abs(dot(normalize(VAR_WORLD_NORMAL), normalize(VIEW_VECTOR))), 0.0, 1.0);
        EMISSIVE_COLOR += vec3(1.0, 0.62, 0.18) * pow(1.0 - ndv, 4.0) * highlight * 0.9;
    }
}
