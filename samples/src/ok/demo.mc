int main() {
    int sum = 1 + 2;
    while (sum < 5 && !done(sum)) {
        sum = inc(sum);
    }
    if (sum >= 5 || sum == 0) {
        sum = sum - 1;
    } else {
        sum = 0;
    }
    return sum;
}

int inc(int x) {
    return x + 1;
}

int done(int x) {
    return x > 9;
}
