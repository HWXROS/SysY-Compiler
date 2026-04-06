int main() {
  int i = 0;
  int j = 0;
  int sum = 0;
  while (i < 5) {
    j = 0;
    while (j < 3) {
      if (j == 1) {
        break;
      }
      sum = sum + 1;
      j = j + 1;
    }
    if (sum > 5) {
      break;
    }
    i = i + 1;
  }
  return sum;
}
