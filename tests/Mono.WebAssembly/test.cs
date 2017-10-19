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

    void test_HtmlDocument()
    {
        var doc = HtmlPage.Document;

        var root = doc.DocumentElement;
        assert(root.TagName == "HTML");
        assert(doc.GetElementsByTagName("html")[0].Equals(root));
        assert(root.Parent == null);

        var body = doc.Body;
        assert(body.TagName == "BODY");
        assert(doc.GetElementsByTagName("body")[0].Equals(body));
        assert(body.Parent.Equals(root));

        // We can't use `Contains()' or even `foreach' yet due to a compiler
        // limitation.
        bool found_body_in_root_children = false;
        var root_children = root.Children;
        for (int i = 0, len = root_children.Count; i < len; i++) {
            var child = root_children[i];
            if (child.Equals(body)) {
                found_body_in_root_children = true;
                break;
            }
        }
        assert(found_body_in_root_children);

        var span_id = doc.GetElementById("span-id");
        assert(span_id != null);
        assert(span_id.TagName == "SPAN");
        assert(span_id.Parent.Equals(body));
        assert(span_id.InnerText == "span-id text");

        assert(doc.GetElementById("does-not-exist") == null);
    }

    void run_tests()
    {
        test_Runtime();
        test_BrowserInformation();
        test_HtmlDocument();

        Console.WriteLine("All tests ({0}) successful", count);
    }

    static void Main()
    {
        var r = new Test();
        r.run_tests();
    }
}
