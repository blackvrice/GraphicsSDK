float4 vertexMain(uint index : SV_VertexID) : SV_Position
{
    float2 position = float2((index << 1) & 2, index & 2);
    return float4(position * 2.0 - 1.0, 0.0, 1.0);
}

float4 pixelMain() : SV_Target0
{
    return float4(1.0, 0.25, 0.0, 1.0);
}
