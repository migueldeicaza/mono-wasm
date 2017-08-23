class Hello
{
    static int factorial(int n)
    {
        if (n == 0) {
            return 1;
        }
        return n * factorial(n - 1);
    }

    static int Main(string[] args)
    {
        int f = factorial(6);
        System.Console.WriteLine("Hello world! factorial(6) -> {0}", f);
        return f;
    }
}
