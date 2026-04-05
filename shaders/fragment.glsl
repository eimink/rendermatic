#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D screenTexture;
uniform sampler2D uvTexture;
uniform int colorFormat; // 0=RGBA, 1=UYVY, 2=UYVA, 3=NV12, 5=DMABUF_NV12

vec4 YUVtoRGB(float Y, float U, float V) {
    // BT.601 full-range conversion matrix
    // Handles both TV range (16-235) and full range (0-255) streams
    // by using the matrix that maps directly without range scaling.
    vec3 yuv = vec3(Y, U - 0.5, V - 0.5);
    vec3 rgb;
    rgb.r = yuv.x + 1.402 * yuv.z;
    rgb.g = yuv.x - 0.344136 * yuv.y - 0.714136 * yuv.z;
    rgb.b = yuv.x + 1.772 * yuv.y;
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
    
    // Check if we're on an odd or even pixel within this UYVY macro-pixel
    bool isOdd = fract(texCoord.x * textureSize(tex, 0).x) >= 0.5;
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

vec4 NV12toRGBA(sampler2D yTex, sampler2D uvTex, vec2 texCoord) {
    float Y = texture(yTex, texCoord).r;
    vec2 uv = texture(uvTex, texCoord).rg;
    return YUVtoRGB(Y, uv.r, uv.g);
}

void main() {
    if (colorFormat == 1) { // UYVY
        FragColor = UYVYtoRGBA(screenTexture, TexCoord);
    }
    else if (colorFormat == 2) { // UYVA
        FragColor = UYVAtoRGBA(screenTexture, TexCoord);
    }
    else if (colorFormat == 3 || colorFormat == 5) { // NV12 or DMABUF_NV12
        FragColor = NV12toRGBA(screenTexture, uvTexture, TexCoord);
    }
    else { // RGBA
        FragColor = texture(screenTexture, TexCoord);
    }
}
