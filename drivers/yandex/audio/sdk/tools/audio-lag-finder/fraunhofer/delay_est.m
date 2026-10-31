function  delay_est( mic,spk,fs, dumpPath )

    % Read inputs
    X = mic;
    Y = spk;
    Fs=fs;
    max_delay =150;
    % Buffering
    n = floor(0.5 * Fs);
    p = floor(3*n/4);
    num_frames = ceil(size(X,1)/(n-p));
    x = zeros(n,num_frames);
    for i=1:size(X,2)
        x(:,:,i) = buffer(X(:,i),n,p);
    end
    y = zeros(n,ceil(size(Y,1)/(n-p)));
    for i=1:size(Y,2)
        y(:,:,i) = buffer(Y(:,i),n,p);
    end

    % Correlation analysis
    d = floor(max_delay/1000 * Fs);
    corr_mat = zeros(2*d+1,size(X,2),size(Y,2),num_frames);
    for i=1:size(X,2)
        for j=1:size(Y,2)
            for k = 1:num_frames
                corr_mat(:,i,j,k) = xcov(x(:,k,i),y(:,k,j),d,'coeff');
            end
        end
    end

    % Plot correlation
    f1 = figure;
    for i=1:size(X,2)
        for j=1:size(Y,2)
            subplot(size(X,2), size(Y,2), size(Y,2)*(i-1)+j)
            imagesc(((1:num_frames)*(n-p)/Fs-n/2/Fs),(-d:d)/Fs*1000,abs(squeeze(corr_mat(:,i,j,:))));
            hold on
            [~,dmax] = max(squeeze(abs(corr_mat(:,i,j,:))),[],1);
            plot(((1:num_frames)*(n-p)/Fs-n/2/Fs),(dmax-d-1)/Fs*1000,'.b')
            hold off
            colormap(flipud(colormap('hot')))
            colorbar
            xlabel('Time (s)')
            ylabel('Correlation-Lag (ms)')
            title(sprintf('C_{mic_%dspk_%d}',i,j));
            grid on
        end
    end
set(f1,'WindowStyle','docked');
saveas(f1, dumpPath);
end

