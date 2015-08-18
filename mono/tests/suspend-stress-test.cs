using System;
using System.Collections.Generic;
using System.Threading;
using System.Diagnostics;
using System.IO;


class Driver {
	static bool stop_please;
	const int TEST_DURATION = 1000;

	static object very_contended_object = new object ();

	static void MonitorEnterInALoop ()
	{
		while (!stop_please) {
			if (Monitor.TryEnter (very_contended_object, 100)) {
				Thread.Sleep (30);
				Monitor.Exit (very_contended_object);
			}
		}
	}

	static void AllocObjectInALoop () {
		while (!stop_please) {
			var a = new object ();
			var b = new byte [100];
		}
	}

	static void AllocDomainInALoop () {
		int count = 0;
		while (!stop_please) {
			var a = AppDomain.CreateDomain ("test_domain_" + ++count);
			AppDomain.Unload (a);
		}
	}

	static void FileIO () {
		while (!stop_please) {
			var dir = Path.GetTempFileName () + "_" + Thread.CurrentThread.ManagedThreadId;
			Directory.CreateDirectory (dir);
			Directory.Delete (dir);
			
		}
		
	}
	static ThreadStart[] available_tests = new [] {
		new ThreadStart (MonitorEnterInALoop),
		new ThreadStart (AllocObjectInALoop),
		new ThreadStart (AllocDomainInALoop),
		new ThreadStart (FileIO),
	};

	static int Main (string[] args) {
		int threadCount = Environment.ProcessorCount - 1;
		int timeInMillis = TEST_DURATION;
		int testIndex = -1;

		for (int j = 0; j < args.Length;) {
			if ((args [j] == "--duration") || (args [j] == "-d")) {
				timeInMillis = Int32.Parse (args [j + 1]);
				j += 2;
			} else if ((args [j] == "--test") || (args [j] == "-t")) {
				if (args [j + 1] == "mix")
					testIndex = -1;
				else
					testIndex = Int32.Parse (args [j + 1]);
				j += 2;
			} else 	if ((args [j] == "--thread-count") || (args [j] == "-tc")) {
				threadCount = Int32.Parse (args [j + 1]);
				j += 2;
			}else {
				Console.WriteLine ("Unknown argument: " + args [j]);
				return 1;
			}
        }

		Console.WriteLine ("thread count {0} duration {1} test {2}", threadCount, timeInMillis, testIndex);

		List<Thread> threads = new List<Thread> ();

		for (int i = 0; i < threadCount; ++i) {
			var t = new Thread (testIndex >= 0 ? available_tests [testIndex] : available_tests [i % available_tests.Length]);
			t.Start ();
			threads.Add (t);
		}

		var sw = Stopwatch.StartNew ();
		do {
			GC.Collect ();
			Thread.Sleep (1);
		} while (sw.ElapsedMilliseconds < timeInMillis);

		stop_please = true;

		foreach (var t in threads)
			t.Join ();
		return 0;
	}
}