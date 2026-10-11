require 'open3'
require 'timeout'
require 'rbconfig'

# The native steps own assertions; this helper supplies portable child I/O.
directory, mode, executable = ARGV
arguments = File.binread(File.join(directory, 'arguments')).split("\n")
input = File.binread(File.join(directory, 'stdin'))
result = nil
errors = +''
output = +''
waited = false
opened_output = false
command = [executable, *arguments]
if mode == 'windows' && ENV['COT_WINDOWS_CLI_PATH']
	command = ['wine', ENV.fetch('COT_WINDOWS_CLI_PATH'), *arguments]
end
Timeout.timeout(mode == 'windows' ? 45 : 15) do
	Dir.chdir(directory) do
		if mode == 'closed_pipe'
			read_end, write_end = IO.pipe
			read_end.close
			File.open('event.xml', 'rb') do |source|
				File.open('pipe.stderr', 'wb') do |error_file|
					pid = Process.spawn(*command, in: source,
						out: write_end, err: error_file)
					write_end.close
					result = Process.waitpid2(pid)[1].exitstatus
				end
			end
			errors = File.binread('pipe.stderr')
		elsif mode == 'pipeline'
			File.open('event.pb', 'rb') do |source|
				File.open('roundtrip.pb', 'wb') do |destination|
					read_end, write_end = IO.pipe
					left_error = File.open('left.stderr', 'wb')
					right_error = File.open('right.stderr', 'wb')
					left = Process.spawn(executable, 'decode', in: source,
						out: write_end, err: left_error)
					right = Process.spawn(executable, 'encode', in: read_end,
						out: destination, err: right_error)
					read_end.close
					write_end.close
					statuses = [Process.waitpid2(left)[1],
						Process.waitpid2(right)[1]]
					result = statuses.all?(&:success?) ? 0 : 1
					left_error.close
					right_error.close
					errors = File.binread('left.stderr') +
						File.binread('right.stderr')
				end
			end
		else
			Open3.popen3(*command) do |stdin, stdout, stderr, child|
				[stdin, stdout, stderr].each(&:binmode)
				reader = mode == 'eof' ? nil : Thread.new { stdout.read }
				error_reader = Thread.new { stderr.read }
				if mode == 'help'
					# Keep input open: help must finish without any EOF.
					result = Timeout.timeout(3) { child.value.exitstatus }
					waited = true
				elsif mode == 'eof'
					stdin.write(input)
					stdin.flush
					sleep 0.15
					waited = child.alive?
					opened_output = File.exist?('result.pb')
					before = stdout.read_nonblock(4096, exception: false)
					before = +'' unless before.is_a?(String)
					File.binwrite('before.stdout', before)
					File.write('ready.tmp', "#{waited}\n#{opened_output}\n")
					File.rename('ready.tmp', 'ready')
					sleep 0.005 until File.exist?('close-pipe')
					reader = Thread.new { before + stdout.read }
					stdin.close
					result = child.value.exitstatus
				else
					begin
						stdin.write(input)
					rescue Errno::EPIPE
					end
					stdin.close
					result = child.value.exitstatus
				end
				stdin.close unless stdin.closed?
				output = reader.value
				errors = error_reader.value
			end
		end
	end
end
File.binwrite(File.join(directory, 'stdout'), output)
File.binwrite(File.join(directory, 'stderr'), errors)
File.write(File.join(directory, 'status'), "#{result}\n#{waited}\n#{opened_output}\n")
