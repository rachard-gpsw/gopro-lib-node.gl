layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout (std430, binding = 0) buffer ipositions_buffer {
    vec3 ipositions[];
};

layout (std430, binding = 1) buffer ivelocities_buffer {
    vec2 ivelocities[];
};

layout (std430, binding = 2) buffer opositions_buffer {
    vec3 opositions[];
};

uniform float time;
uniform float duration;

float bounceOut(float t)
{
    float c = 1.0;
    float a = 1.70158;

    if (t >= 1.0) {
        return c;
    } else if (t < 4.0 / 11.0) {
        return c * (7.5625 * t * t);
    } else if (t < 8.0 / 11.0) {
        t -= 6.0 / 11.0;
        return -a * (1.0 - (7.5625 * t * t + 0.75)) + c;
    } else if (t < 10.0 / 11.0) {
        t -= 9.0 / 11.0;
        return -a * (1.0 - (7.5625 * t * t + 0.9375)) + c;
    } else {
        t -= 21.0 / 22.0;
        return -a * (1.0 - (7.5625 * t * t + 0.984375)) + c;
    }
}

float bounce(float t)
{
    return 1.0 - bounceOut(t);
}

void main(void)
{
    uint i = gl_GlobalInvocationID.x +
             gl_GlobalInvocationID.y * 1024U;

    vec3 iposition = ipositions[i];
    vec2 ivelocity = ivelocities[i];
    float step = time * duration * 30.0;
    vec2 velocity = ivelocity;
    vec3 position = iposition;
    float yoffset = 1.0 - iposition.y;
    float speed = 1.0 + ivelocity.y;

    position.x = iposition.x + step * velocity.x;
    position.y = ((2.0 - yoffset) * bounce(time * speed * (1.0  + yoffset))) - 0.99;

    opositions[i] = position;
}
