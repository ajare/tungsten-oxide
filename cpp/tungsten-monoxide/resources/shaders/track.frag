@@Version

@@Uniform(vec4 DIFFUSE);
@@Texture(sampler2D TEX1);

void main()
{
	vec4 colour = @Vec4(@In(COLOUR));
    	colour *= @Uniform(DIFFUSE);

	vec2 tc = @In(TEXCOORDS).st;

	@Out(vec4 COLOUR) = texture(@Texture(TEX1), tc) * colour;
}