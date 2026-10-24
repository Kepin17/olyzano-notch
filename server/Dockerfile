FROM python:3.13-alpine
RUN adduser -D -u 10001 relay && mkdir /data && chown relay /data
WORKDIR /app
COPY relay.py page.html join.html ./
USER relay
ENV DATA_DIR=/data PORT=8080
EXPOSE 8080
CMD ["python", "-u", "relay.py"]
