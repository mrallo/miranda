FROM alpine:latest

# Install necessary dependencies
RUN apk add --no-cache bash libc6-compat tar

# Define the archive name
ENV MIRANDA_TGZ="miranda.tgz"

# Copy the specified version archive
COPY dist/miranda-*.tgz /${MIRANDA_TGZ}

# Extract the archive as root in the image root
USER root
RUN tar -xzf /${MIRANDA_TGZ} -C / && \
    chown root:root -R /usr/local && \
    rm /${MIRANDA_TGZ}

# Create a non-root user
RUN adduser -D -u 1000 miranda

# Switch to the non-root user for execution
USER miranda

# Set the default command
ENTRYPOINT ["/usr/local/bin/mira"]
