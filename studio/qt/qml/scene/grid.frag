// Floor grid for the standalone viewport: 1 m cells, 5 m majors, fading with distance from the camera.
float gridLine(vec2 p, float spacing, float width)
{
    vec2 g = abs(fract(p / spacing - 0.5) - 0.5) * spacing;
    vec2 d = fwidth(p) * width;
    vec2 l = 1.0 - smoothstep(vec2(0.0), d, g);
    return max(l.x, l.y);
}

void MAIN()
{
    vec2 p = VAR_WORLD_POSITION.xz;
    float minor = gridLine(p, 1.0, 1.0) * 0.45;
    float major = gridLine(p, 5.0, 1.4);
    float dist = length(VAR_WORLD_POSITION - CAMERA_POSITION);
    float fade = 1.0 - smoothstep(12.0, 60.0, dist);
    float line = max(minor, major) * fade;
    BASE_COLOR = vec4(mix(baseTint.rgb, lineTint.rgb, line), 1.0);
    // the world's axes on the floor, like Blender's: X red (z = 0), Z blue (x = 0)
    float ax = (1.0 - smoothstep(0.0, fwidth(p.y) * 1.5, abs(p.y))) * fade;
    float az = (1.0 - smoothstep(0.0, fwidth(p.x) * 1.5, abs(p.x))) * fade;
    EMISSIVE_COLOR = vec3(0.55, 0.08, 0.12) * ax + vec3(0.06, 0.22, 0.45) * az;
    ROUGHNESS = 0.95;
    METALNESS = 0.0;
    SPECULAR_AMOUNT = 0.2;
}
