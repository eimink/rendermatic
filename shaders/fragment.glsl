#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTexture;
uniform int colorFormat; // 0=RGBA, 1=UYVY, 2=UYVA

vec4 YUVtoRGB(float Y, float Cb, float Cr) {
    // BT.709 conversion matrix
    vec3 rgb;
    Y = 1.164383561643836 * (Y - 0.0625);
    Cb = Cb - 0.5;
    Cr = Cr - 0.5;
    
    rgb.r = Y + 1.792741071428571 * Cr;
    rgb.g = Y - 0.532909328559444 * Cr - 0.213248614273730 * Cb;
    rgb.b = Y + 2.112401785714286 * Cb;
    
    return vec4(clamp(rgb, 0.0, 1.0), 1.0);
}

vec4 UYVYtoRGBA(sampler2D tex, vec2 texCoord) {
    float texelWidth = 1.0 / textureSize(tex, 0).x;
    vec2 evenTexCoord = vec2(floor(texCoord.x * textureSize(tex, 0).x) * texelWidth, texCoord.y);
    
    vec4 texel = texture(tex, evenTexCoord);
    float U = texel.r;
    float Y1 = texel.g;
    float V = texel.b;
    float Y2 = texel.a;
    
    // Check if we're on an odd or even pixel
    bool isOdd = mod(texCoord.x * textureSize(tex, 0).x, 2.0) >= 1.0;
    float Y = isOdd ? Y2 : Y1;
    
    return YUVtoRGB(Y, U, V);
}

vec4 UYVAtoRGBA(sampler2D tex, vec2 texCoord) {
    vec4 texel = texture(tex, texCoord);
    float U = texel.r;
    float Y = texel.g;
    float V = texel.b;
    float A = texel.a;
    
    vec4 rgb = YUVtoRGB(Y, U, V);
    rgb.a = A;
    return rgb;
}

void main() {
    if (colorFormat == 1) { // UYVY
        FragColor = UYVYtoRGBA(screenTexture, TexCoord);
    }
    else if (colorFormat == 2) { // UYVA
        FragColor = UYVAtoRGBA(screenTexture, TexCoord);
    }
    else { // RGBA
        FragColor = texture(screenTexture, TexCoord);
    }
}
