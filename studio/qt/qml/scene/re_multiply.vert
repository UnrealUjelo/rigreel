// Unshaded materials get no UV0 in the fragment shader: hand it over (VERTEX is already skinned here)
VARYING vec2 texcoord;

void MAIN()
{
    texcoord = UV0;
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
}
