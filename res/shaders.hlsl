struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

PSInput VSMain(float4 pos : POSITION, float4 col : COLOR)
{
    PSInput result;

    result.pos = pos;
    result.col = col;

    return result;
}

float4 PSMain(PSInput input): SV_TARGET
{
    return input.col;
}