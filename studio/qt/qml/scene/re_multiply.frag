// Multiply layer: 1 where the texture's alpha is 0, darker as it rises (see ReMultiply.qml)
VARYING vec2 texcoord;

void MAIN()
{
    vec4 t = texture(baseMap, texcoord);
    float keep = 1.0 - clamp(t.a, 0.0, 1.0) * strength;
    FRAGCOLOR = vec4(mix(vec3(1.0), t.rgb, clamp(t.a, 0.0, 1.0)) * keep, 1.0);
    if (clay > 0.5) FRAGCOLOR = vec4(1.0);          // Solid shading: nothing to darken
}
