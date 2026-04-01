int main() {
  int x = 10;
  {
    int y = 20;
    x = x + y;
    {
      int z = 30;
      x = x + y + z;
    }
  }
  return x;
}
