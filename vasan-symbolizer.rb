#!/usr/bin/env ruby

require 'pty'
require 'digest/md5'

@current_trace=""
@known_traces=Hash.new

printf("Remember to run this program from the working directory of your program!!!\n");

if ARGV.size < 1
  print("syntax: #{PROGRAM_NAME} log_file\n")
  exit
end

def symbolize()
  tmp=""
  @current_trace.each_line { |line|
    if line.include? "[0x"
      num=line.split("[")[1].split("]")[0]
      addr=line.split("[")[2].split("]")[0]
      bin=line.split("(")[0].split("]")[1].lstrip
      dbg=`echo #{addr} | addr2line -e #{bin} -f -p -C`
      if dbg.include? "??"
        func=line.split("(")[1].split(")")[0]
        tmp+="[#{num}] #{addr}: #{func} in #{bin} (no debug syms found)\n"
      else
        tmp+="[#{num}] #{addr}: #{dbg}"
      end
    else
      tmp+= line
    end
  }
  @current_trace = tmp
end


File.open("#{ARGV[0]}.filtered", 'w') { |file|
  `cat #{ARGV[0]}`.each_line { |line|
    if line.start_with? "---"
      hash=Digest::MD5.hexdigest(@current_trace)
      if not @known_traces[hash]
        symbolize()
        file.write(@current_trace)
        @known_traces[hash]=1
      else
        @known_traces[hash]+=1
      end
      @current_trace=line
    else
      @current_trace+=line
    end
  }
}

print("Wrote #{ARGV[0]}.filtered!\n")
