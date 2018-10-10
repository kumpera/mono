using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNetCore;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;

namespace WsProxy
{
    public class Program
    {
        public static void Main(string[] args)
        {
			var hostConfig = new WebHostBuilder();
			if (args.Length > 0)
				hostConfig.UseSetting("mono.nodejs.app", args[0]);
			hostConfig.UseSetting ("mono.nodejs.app","Zzz");

			var host = hostConfig
				.UseSetting(nameof(WebHostBuilderIISExtensions.UseIISIntegration), false.ToString())
				.ConfigureAppConfiguration (config => config.AddCommandLine (args))
		        .UseKestrel()
		        .UseContentRoot(Directory.GetCurrentDirectory())
		        .UseStartup<Startup>()
		        .UseUrls("http://localhost:9300")
		        .Build();

		    host.Run();
        }
    }
}
