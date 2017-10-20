using Mono.WebAssembly;
using System;

class Test
{
    int count = 0;
    int failures = 0;

    void _assert(bool condition, string msg)
    {
        if (condition) {
            count++;        
        }
        else {
            Console.WriteLine(msg);
            failures++;
        }
    }

    void assert(bool condition)
    {
        _assert(condition, $"assertion {count} failed");
    }

    void assert_Equals(object obj1, object obj2)
    {
        _assert(((obj1 == null && obj2 == null) || obj1.Equals(obj2)),
                $"assertion {count} failed: `{obj1}' should be equal to `{obj2}'");
    }

    void test_Runtime()
    {
        assert_Equals(Runtime.JavaScriptEval("1+2") , "3");

        assert_Equals(Runtime.JavaScriptEval("var x = 42; x"), "42");

        assert_Equals(Runtime.JavaScriptEval(
                    "(function(x, y) { return x + y; })(40, 2);"), "42");
    }

    void test_BrowserInformation()
    {
        var bi = HtmlPage.BrowserInformation;

        assert_Equals(bi.Name, "Netscape");
        assert(bi.BrowserVersion.Contains("5.0"));
        assert(bi.UserAgent.Contains("Firefox")
                || bi.UserAgent.Contains("Chrome")
                || bi.UserAgent.Contains("Safari"));
        assert_Equals(bi.Platform, "MacIntel");
        assert_Equals(bi.CookiesEnabled, true);
        assert_Equals(bi.ProductName, "Mozilla");
    }

    void test_HtmlDocument()
    {
        var doc = HtmlPage.Document;

        var root = doc.DocumentElement;
        assert_Equals(root.TagName, "HTML");
        assert_Equals(doc.GetElementsByTagName("html")[0], root);
        assert_Equals(root.Parent, null);

        var body = doc.Body;
        assert_Equals(body.TagName, "BODY");
        assert_Equals(doc.GetElementsByTagName("body")[0], body);
        assert_Equals(body.Parent, root);

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
        assert_Equals(span_id.TagName, "SPAN");
        assert_Equals(span_id.Id, "span-id");
        assert_Equals(span_id.Parent, body);
        assert_Equals(span_id.InnerText, "span-id text");

        assert_Equals(doc.GetElementById("does-not-exist"), null);

        var elem = doc.CreateElement("span");
        elem.Id = "span-id2";
        assert_Equals(elem.TagName, "SPAN");
        assert_Equals(elem.Parent, null);
        body.AppendChild(elem);
        assert_Equals(elem.Parent, body);
        assert_Equals(doc.GetElementById("span-id2"), elem);
        body.RemoveChild(elem);
        assert_Equals(elem.Parent, null);
        assert_Equals(doc.GetElementById("span-id2"), null);
    }

    void test_HtmlNode()
    {
        var doc = HtmlPage.Document;

        assert_Equals(doc.GetElementsByTagName("does-not-exist").Count, 0);
        assert_Equals(doc.Body.GetElementsByTagName("does-not-exist").Count, 0);

        assert_Equals(doc.GetElementsByTagName("p").Count, 3);
        assert_Equals(doc.Body.GetElementsByTagName("p").Count, 3);

        assert_Equals(doc.GetElementsByClassName("my-class").Count, 4);
        assert_Equals(doc.Body.GetElementsByClassName("my-class").Count, 4);
        var ary = doc.GetElementsByClassName("container");
        var ary2 = doc.Body.GetElementsByClassName("container");
        assert_Equals(ary.Count, 1);
        assert_Equals(ary2.Count, 1);
        var container = ary[0];
        assert_Equals(container, ary2[0]);
        assert_Equals(container.GetElementsByClassName("my-class").Count, 1);
        assert_Equals(container.InnerText, "my-class-container\ntext");

        var ary3 = container.GetElementsByTagName("span");
        assert_Equals(ary3.Count, 1);
        ary3[0].InnerText = "42";
        assert_Equals(container.InnerText, "my-class-container\n42");

        assert_Equals(doc.QuerySelector(".does-not-exist"), null);
        assert_Equals(doc.Body.QuerySelector(".does-not-exist"), null);

        var elem = doc.QuerySelector(".my-class");
        assert(elem != null);
        assert_Equals(elem, doc.QuerySelector(".container"));
        assert_Equals(elem, doc.Body.QuerySelector(".my-class"));
        assert_Equals(elem, doc.Body.QuerySelector(".container"));

        var ary4 = doc.QuerySelectorAll(".my-class");
        var ary5 = doc.Body.QuerySelectorAll(".my-class");
        assert_Equals(ary4.Count, 4);
        assert_Equals(ary5.Count, 4);
        assert_Equals(ary4[0], elem);
        assert_Equals(ary5[0], elem);

        assert_Equals(doc.QuerySelector(".container span"), ary3[0]);
    }

    void test_HtmlElement()
    {
        var doc = HtmlPage.Document;

        var elem = doc.CreateElement("div");
        assert_Equals(elem.TagName, "DIV");
        assert_Equals(elem.ClassName, "");
        assert_Equals(elem.Id, "");
        assert_Equals(elem.Parent, null);
        assert_Equals(elem.Children.Count, 0);
        assert_Equals(elem.AttributeNames.Length, 0);
        assert_Equals(elem.InnerText, "");

        elem.InnerText = "foo";
        assert_Equals(elem.InnerText, "foo");

        assert_Equals(elem.GetAttribute("does-not-exist"), null);

        elem.ClassName = "my-class";
        assert_Equals(elem.ClassName, "my-class");
        assert_Equals(elem.AttributeNames.Length, 1);
        assert_Equals(elem.AttributeNames[0], "class");
        assert_Equals(elem.GetAttribute("class"), "my-class");

        elem.SetAttribute("id", "my-id");
        assert_Equals(elem.Id, "my-id");
        assert_Equals(elem.AttributeNames.Length, 2);

        elem.RemoveAttribute("id");
        assert_Equals(elem.Id, "");
        assert_Equals(elem.AttributeNames.Length, 1);

        var ary = doc.GetElementsByTagName("script");
        assert_Equals(ary.Count, 1);
        var elem2 = ary[0];
        assert_Equals(elem2.AttributeNames.Length, 1);
        assert_Equals(elem.AttributeNames[0], "src");
        assert_Equals(elem.GetAttribute("src"), "index.js");
    }

    void run_tests()
    {
        test_Runtime();
        test_BrowserInformation();
        test_HtmlDocument();
        test_HtmlNode();
        test_HtmlElement();

        if (failures == 0) {
            Console.WriteLine("All tests ({0}) successful", count);
        }
        else {
            Console.WriteLine("Tests ran with {0} failures", failures);
        }
    }

    static void Main()
    {
        var r = new Test();
        r.run_tests();
    }
}
