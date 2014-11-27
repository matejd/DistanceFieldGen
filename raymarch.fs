uniform sampler2D distFieldSam;
const float distFieldSize = 64.0;
uniform vec2 canvasSize;
uniform vec3 origin;
uniform float time;

float sampleDistanceField(vec3 position)
{
    // Sample at two points (two y slices) and interpolate. This emulates
    // bilinear interpolation for 3D texture.
    vec3 coords = clamp(position, 0.0, 1.0);
    vec2 c1 = vec2((floor(coords.z*distFieldSize) + coords.x) / distFieldSize,       coords.y);
    vec2 c2 = vec2((floor(coords.z*distFieldSize) + 1.0 + coords.x) / distFieldSize, coords.y);
    float d1 = texture2D(distFieldSam, c1).r - 0.5; // Assuming signed distance field (values from -0.5 to 0.5).
    float d2 = texture2D(distFieldSam, c2).r - 0.5;
    float a = fract(coords.z*distFieldSize);
    return mix(d1, d2, a);
}

float rand(vec3 co)
{
    return fract(sin(dot(co, vec3(12.9898,78.233,91.1743))) * 43758.5453);
}

float march(vec3 ro, vec3 rd, float randOff)
{
    float result = 0.0;
    // Start marching closer to the mesh - first part of the line below
    // computes the closest distance from origin to unit cube. We additionally add
    // some random value to hide undersampling with noise.
    float t = length(max(abs(ro-vec3(0.5))-vec3(0.5), 0.0)) + 0.1 + randOff;
    for (int i = 0; i < 20; i++) {
        vec3 p = ro + rd*t;
        if (all(lessThan(p, vec3(1.0))) &&
            all(greaterThan(p, vec3(0.0)))) {
            float dist = sampleDistanceField(p);
            if (dist < 0.0)
                result += 4.0/20.0; // Count samples "inside" the mesh.
        }
        t += sqrt(2.0)/20.0;
    }
    return result;
}

void main()
{
    float ratio = canvasSize.y / canvasSize.x;
    float halfWidth = canvasSize.x / 2.f;
    float halfHeight = canvasSize.y / 2.f;
    float x = (gl_FragCoord.x - halfWidth)  / canvasSize.x;
    float y = ratio * (gl_FragCoord.y - halfHeight) / canvasSize.y;

    // Random offset - value between 0 and 0.2.
    float randOff = rand(vec3(gl_FragCoord.xy / canvasSize, time)) * 0.2;

    // Get ray origin and ray direction from camera position (origin) and
    // target (0.5, 0.5, 0.5).
    vec3 ro = origin.xzy; // Flip y and z.
    vec3 ww = normalize(vec3(0.5)-ro);
    vec3 uu = normalize(cross(ww, vec3(0.0, 1.0, 0.0)));
    vec3 vv = normalize(cross(uu, ww));
    vec3 rd = normalize(x*uu + y*vv + 2.5*ww);

    float t = march(ro, rd, randOff);

    vec3 color;
    // Background
    float xn = x+0.5;
    float yn = y/ratio+0.5;
    vec2 dist = vec2(0.5-xn,0.5-yn);
    float strength = 1.0 - dot(dist, dist);
    color = randOff * pow(strength, 3.0) * vec3(1.0); // Noise in the background as well.
    color += vec3(0.0, t, 0.0);
    gl_FragColor = vec4(color, 1.0);
}
