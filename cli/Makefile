target := target
binary := thunderobot

all: build

build:
	cargo build --release

clean:
	rm -rf $(target)

install:
	if [ -d target/release ]; then \
		install -Dm755 target/release/$(binary) /usr/bin/$(binary);\
		else \
			install -Dm755 target/debug/$(binary) /usr/bin/$(binary);\
			fi

