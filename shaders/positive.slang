[[vk::push_constant]] uint64_t* out;
[numthreads(1,1,1)]
void main() {
  uint64_t* sp = out + 200;
  sp[(int64_t)5] = 22;
  out[191] = sp[(int64_t)5];
}
