﻿using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.AspNetCore;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;

namespace WsProxy
{
    public class Program
    {
        public static void _Main(string[] args)
        {
			var host = new WebHostBuilder()
				.UseSetting(nameof(WebHostBuilderIISExtensions.UseIISIntegration), false.ToString())
		        .UseKestrel()
		        .UseContentRoot(Directory.GetCurrentDirectory())
		        .UseStartup<Startup>()
		        .UseUrls("http://localhost:9300")
		        .Build();
		    host.Run();
        }
    }

	public class TestHarnessProxy {
		static IWebHost host;

        public static void Main(string[] args)
        {
			Start ("/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
				"/Users/kumpera/src/mono/sdks/wasm/bin/debugger-test-suite/",
				"debugger-driver.html");

			Thread.Sleep (5000 * 1000);
        }

		public static void Start (string chrome_path, string app_path, string page_path) {
			lock (typeof (TestHarnessProxy)) {
				if (host != null)
					return;

				//FIXME wtf ConfigureAppConfiguration
				string[] args = new [] {
					$"/ChromePath={chrome_path}",
					$"/AppPath={app_path}",
					$"/PagePath={page_path}",
				};

				var h = new WebHostBuilder()
					.UseSetting(nameof(WebHostBuilderIISExtensions.UseIISIntegration), false.ToString())
					.ConfigureAppConfiguration (config => config.AddCommandLine (args))
			        .UseKestrel()
			        .UseStartup<TestHarnessStartup>()
			        .UseUrls("http://localhost:9300")
			        .Build();
				host = h;
				Task.Run (() => { host.Run (); });

				Thread.Sleep (1000);
				//FIXME implement this using socket polling so it's faster
				Console.WriteLine ("WebServer Ready!");
			}
		}
	}
}
