#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;

uniform int displayRotation; // 0=0°, 1=90°CW, 2=180°, 3=270°CW

void main() {
    vec3 pos = aPos;
    vec2 tc = aTexCoord;

    if (displayRotation == 1) {
        pos = vec3(-aPos.y, aPos.x, aPos.z);
        tc = vec2(aTexCoord.y, 1.0 - aTexCoord.x);
    } else if (displayRotation == 2) {
        pos = vec3(-aPos.x, -aPos.y, aPos.z);
        tc = vec2(1.0 - aTexCoord.x, 1.0 - aTexCoord.y);
    } else if (displayRotation == 3) {
        pos = vec3(aPos.y, -aPos.x, aPos.z);
        tc = vec2(1.0 - aTexCoord.y, aTexCoord.x);
    }

    gl_Position = vec4(pos, 1.0);
    TexCoord = tc;
}
