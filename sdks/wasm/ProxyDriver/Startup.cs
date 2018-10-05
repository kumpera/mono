using System;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.AspNetCore.Routing;
using System.Net.WebSockets;
using System.Threading;
using System.IO;
using System.Text;

using  Microsoft.AspNetCore.Http;
using Newtonsoft.Json.Linq;

namespace WsProxy
{

	internal class Startup {
		// This method gets called by the runtime. Use this method to add services to the container.
		// For more information on how to configure your application, visit https://go.microsoft.com/fwlink/?LinkID=398940
		public void ConfigureServices (IServiceCollection services)
		{
			services.AddRouting ();
		}

		public static async Task ProxyMsg (string desc, WebSocket from, WebSocket to)
		{
			byte [] buff = new byte [4000];
			var mem = new MemoryStream ();
			while (true) {
				var result = await from.ReceiveAsync (new ArraySegment<byte> (buff), CancellationToken.None);
				if (result.MessageType == WebSocketMessageType.Close) {
					await to.SendAsync (new ArraySegment<byte> (mem.GetBuffer (), 0, (int)mem.Length), WebSocketMessageType.Close, true, CancellationToken.None);
					return;
				}

				if (result.EndOfMessage) {
					mem.Write (buff, 0, result.Count);

					var str = Encoding.UTF8.GetString (mem.GetBuffer (), 0, (int)mem.Length);

					await to.SendAsync (new ArraySegment<byte> (mem.GetBuffer (), 0, (int)mem.Length), WebSocketMessageType.Text, true, CancellationToken.None);
					mem.SetLength (0);
				} else {
					mem.Write (buff, 0, result.Count);
				}
			}
		}

		Uri GetBrowserUri (string path)
		{
			return new Uri ("ws://localhost:9222" + path);
		}

		async Task SendNodeVersion (HttpContext context) {
			Console.WriteLine ("hello chrome! json/version");
			var resp_obj = new JObject();
			resp_obj["Browser"] = "node.js/v9.11.1";
			resp_obj["Protocol-Version"] = "1.1";

			var response = resp_obj.ToString ();
			await context.Response.WriteAsync (response, new CancellationTokenSource ().Token);
		}
		async Task Halp (HttpContext context) {
			Console.WriteLine ("Halp! ");
			await context.Response.WriteAsync ("SOMETHING", new CancellationTokenSource ().Token);
		}

		async Task SendNodeList (HttpContext context) {
			Console.WriteLine ("hello chrome! json/list");
			try {
			var response = new JArray (JObject.FromObject (new {
				description = "node.js instance",
				devtoolsFrontendUrl = "chrome-devtools://devtools/bundled/inspector.html?experiments=true&v8only=true&ws=localhost:9300/91d87807-8a81-4f49-878c-a5604103b0a4",
				faviconUrl = "https://nodejs.org/static/favicon.ico",
				id =  "91d87807-8a81-4f49-878c-a5604103b0a4",
				title = "foo.js",
				type = "node",
				url = "file:///Users/kumpera/src/mono/sdks/wasm/dbg/foo.js",
				webSocketDebuggerUrl = "ws://localhost:9300/91d87807-8a81-4f49-878c-a5604103b0a4"
			})).ToString ();

			Console.WriteLine ($"sending: {response}");
			await context.Response.WriteAsync (response, new CancellationTokenSource ().Token);
			} catch (Exception e) {Console.WriteLine(e);}
		}

		// This method gets called by the runtime. Use this method to configure the HTTP request pipeline.
		public void Configure (IApplicationBuilder app, IHostingEnvironment env)
		{
			app.UseDeveloperExceptionPage ();

			app.UseWebSockets ();
			
			// var trackPackageRouteHandler = new RouteHandler(context =>
			// {
			// 	Console.WriteLine ("---trackPackageRouteHandler");
			// 	var routeValues = context.GetRouteData().Values;
			//
			// 	Console.WriteLine($"aqui {string.Join(", ", routeValues)}");
			//     return context.Response.WriteAsync(
			//         $"Hello! Route values: {string.Join(", ", routeValues)}");
			// });
			//
			// var routeBuilder = new RouteBuilder(app, trackPackageRouteHandler);
			// routeBuilder.MapGet ("json", SendNodeList);
			// routeBuilder.MapGet ("json/list", SendNodeList);
			// routeBuilder.MapGet ("json/version", SendNodeVersion);
			// routeBuilder.MapGet ("91d87807-8a81-4f49-878c-a5604103b0a4", Halp);
			//
			// var routes = routeBuilder.Build();
			// app.UseRouter(routes);
			app.UseRouter (router => {
				router.MapGet ("devtools/page/{pageId}", async context => {
					if (!context.WebSockets.IsWebSocketRequest) {
						context.Response.StatusCode = 400;
						return;
					}

					try {
						var proxy = new MonoProxy ();
						var browserUri = GetBrowserUri (context.Request.Path.ToString ());
						var ideSocket = await context.WebSockets.AcceptWebSocketAsync ();

						await proxy.Run (browserUri, ideSocket);
					} catch (Exception e) {
						Console.WriteLine ("got exception {0}", e);
					}
				});
				//Inspector API for using chrome devtools directly
				router.MapGet ("json", SendNodeList);
				router.MapGet ("json/list", SendNodeList);
				router.MapGet ("json/version", SendNodeVersion);
				router.MapGet ("91d87807-8a81-4f49-878c-a5604103b0a4", async context => {
					if (!context.WebSockets.IsWebSocketRequest) {
						context.Response.StatusCode = 400;
						return;
					}

					try {
						var proxy = new MonoProxy ();
						// var browserUri = GetBrowserUri (context.Request.Path.ToString ());
						var browserUri = new Uri ("ws://127.0.0.1:9229/5396589f-0696-4572-8027-0d4dacdb083c");
						var ideSocket = await context.WebSockets.AcceptWebSocketAsync ();

						await proxy.Run (browserUri, ideSocket);
					} catch (Exception e) {
						Console.WriteLine ("got exception {0}", e);
					}
				});
			});
		}
	}
}
