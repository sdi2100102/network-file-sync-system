FROM gcc:12

WORKDIR /app

COPY . .

RUN make

CMD ["make run"]