using Mono.WebAssembly;
using System;

class Test
{
    int count = 0;

    void assert(bool condition)
    {
        if (condition) {
            count++;        
        }
        else {
            throw new SystemException("failed condition");
        }
    }

    void test_Runtime()
    {
        assert(Runtime.JavaScriptEval("1+2") == "3");

        assert(Runtime.JavaScriptEval("var x = 42; x") == "42");

        assert(Runtime.JavaScriptEval(
                    "(function(x, y) { return x + y; })(40, 2);") == "42");
    }

    void run_tests()
    {
        test_Runtime();

        Console.WriteLine("All tests ({0}) successful", count);
    }

    static void Main()
    {
        var r = new Test();
        r.run_tests();
    }
}
