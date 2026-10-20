float4 VS(uint vertex : SV_VertexID) : SV_Position {
    float2 positions[3] = { float2(-0.8, -0.8), float2(0.0, 0.8), float2(0.8, -0.8) };
    return float4(positions[vertex % 3], 0.5, 1.0);
}
float4 PS() : SV_Target0 { return float4(1, 0, 0, 1); }
