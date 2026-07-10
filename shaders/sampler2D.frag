uniform sampler2D renderedImage;

in vec2 TexCoord;

out vec4 FragColor;

void main() {
    FragColor = texture(renderedImage, TexCoord);
}