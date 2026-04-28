<?php

$key = filter_input(INPUT_POST, "key");
$postdata = file_get_contents("php://input");

// Create a stream
$url = 'http://localhost:8210/' . $key;

$options = array(
    'http' => array(
        'method'  => 'PUT',
        'header'  =>
            "Content-type:raw\r\n".
            "Accept: raw\r\n".
            "Connection: close\r\n".
            "Content-length: " . strlen($request) . "\r\n",
        'content' => $json_data
    )
);

$context = stream_context_create($options);
$response = file_get_contents($url, false, $context);
echo json_encode($response);
