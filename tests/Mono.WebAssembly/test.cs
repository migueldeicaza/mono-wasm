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

    void test_BrowserInformation()
    {
        var bi = HtmlPage.BrowserInformation;

        assert(bi.Name == "Netscape");
        assert(bi.BrowserVersion.Contains("5.0"));
        assert(bi.UserAgent.Contains("Firefox")
                || bi.UserAgent.Contains("Chrome")
                || bi.UserAgent.Contains("Safari"));
        assert(bi.Platform == "MacIntel");
        assert(bi.CookiesEnabled == true);
        assert(bi.ProductName == "Mozilla");
    }

    void run_tests()
    {
        test_Runtime();
        test_BrowserInformation();

        Console.WriteLine("All tests ({0}) successful", count);
    }

    static void Main()
    {
        var r = new Test();
        r.run_tests();
    }
}
