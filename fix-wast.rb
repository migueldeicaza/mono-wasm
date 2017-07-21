# This should no longer be needed once we compile mono with a wasm target.

if ARGV.size != 1
  $stderr.puts "Usage: #{__FILE__} file.wast"
  exit 1
end

t = File.read(ARGV.first)

t.scan(/"_([^\$]+)\$(UNIX2003|DARWIN_EXTSN|INODE64)"/).each do |comp, ext|
  if s = t.index("(import \"env\" \"#{comp}\"")
    e = s + 1
    while t[e] != "\n"
      e += 1
    end
    t[s..e] = ''
  end
  t.gsub!(/_#{comp}\$#{ext}/, comp)

  if t.index("(export \"#{comp}\" ")
    t.gsub!(/\(import "env" "#{comp}".*$/, '')
  end
end

#t.gsub!(/\(import "env" "_exit"/, '(import "env" "exit"')

File.open(ARGV.first, 'w') { |io| io.write(t) }
