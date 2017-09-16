class Hello
{
    static int Factorial(int n)
    {
        if (n == 0) {
            return 1;
        }
        return n * Factorial(n - 1);
    }

    // This function is called from the browser by JavaScript.
    // Here we calculate the factorial of the given number then use the
    // Mono.WebAssembly.JavaScriptEval() method to evaluate a JavaScript
    // expression that will set the result as the text of a given element in
    // the DOM.
    static void FactorialInElement(int n, string element_id)
    {
        int f = Factorial(n);
        string expr = System.String.Format(
                "document.getElementById(\"{0}\").innerText = \"{1}\";",
                element_id, f);
        Mono.WebAssembly.JavaScriptEval(expr);
    }

    static int Main(string[] args)
    {
        int f = Factorial(6);
        System.Console.WriteLine("Hello world! factorial(6) -> {0}", f);
        if (args.Length > 0) FactorialInElement(0, ""); // this is a hack so that the linker does not remove the FactorialInElement() method
        return f;
    }
}
