layout(location = 1) flat in uint inPickId;

layout(location = 0) out uint outPickId;

void main()
{
  outPickId = inPickId;
}
